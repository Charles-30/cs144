#include "tcp_connection.hh"

#include <iostream>

using namespace std;

// 将 Sender 的段加上 Receiver 的 ACK 信息，并放入 _segments_out
void TCPConnection::_trans_segments() {
    while (!_sender.segments_out().empty()) {
        TCPSegment seg = _sender.segments_out().front();
        _sender.segments_out().pop();

        // 捎带 ACK (Piggyback)
        if (_receiver.ackno().has_value()) {
            seg.header().ack = true;
            seg.header().ackno = _receiver.ackno().value();
            seg.header().win = _receiver.window_size();
        }

        _segments_out.push(seg);
    }
}

// 检查是否满足“干净关闭”的条件
void TCPConnection::_check_clean_shutdown() {
    // 1. 入站流结束 (收到 FIN)
    // 2. 出站流结束 (应用层写完了)
    // 3. 出站流已完全确认 (对方收到了 FIN)
    if (_receiver.stream_out().input_ended() && 
        _sender.stream_in().eof() && 
        _sender.bytes_in_flight() == 0) {
        
        // 如果不需要 linger，或者 linger 时间已到
        if (!_linger_after_streams_finish || 
            _time_since_last_segment_received >= 10 * _cfg.rt_timeout) {
            _is_active = false;
        }
    }
}

size_t TCPConnection::remaining_outbound_capacity() const { 
    return _sender.stream_in().remaining_capacity(); 
}

size_t TCPConnection::bytes_in_flight() const { 
    return _sender.bytes_in_flight(); 
}

size_t TCPConnection::unassembled_bytes() const { 
    return _receiver.unassembled_bytes(); 
}

size_t TCPConnection::time_since_last_segment_received() const { 
    return _time_since_last_segment_received; 
}

void TCPConnection::segment_received(const TCPSegment &seg) {
    if (!_is_active) {
        return;
    }

    _time_since_last_segment_received = 0;

    // 1. 先让 Receiver 判断包是否有效 (窗口检查)
    bool accept_success = _receiver.segment_received(seg);

    // 2. 处理 RST
    if (seg.header().rst) {
        // 情况 A: LISTEN 状态 (Receiver 空，Sender 也没发数据)
        // RFC 规定：忽略 RST
        if (!_receiver.ackno().has_value() && _sender.next_seqno_absolute() == 0) {
            return;
        }

        // 情况 B: SYN_SENT 状态 (Receiver 空，但 Sender 已发 SYN)
        // 场景：Client 发起连接，对方拒绝 (RST+ACK)
        // 此时 Receiver 可能会因为没收到 SYN 而让 accept_success = false，
        // 但我们必须处理这个 RST。
        if (!_receiver.ackno().has_value() && _sender.next_seqno_absolute() > 0) {
            if (seg.header().ack) {
                // 收到 RST + ACK，说明对方拒绝了我们的 SYN
                _receiver.stream_out().set_error();
                _sender.stream_in().set_error();
                _is_active = false;
                return;
            }
            // 如果只有 RST 没有 ACK，RFC 建议丢弃
            return;
        }

        // 情况 C: 已连接状态 (ESTABLISHED 等)
        // 只有合法的 RST (在窗口内) 才会断开连接
        if (accept_success) {
            _receiver.stream_out().set_error();
            _sender.stream_in().set_error();
            _is_active = false;
            return;
        }
        
        // 非法 RST (accept_success=false) 继续向下走，触发 Challenge ACK
    }

    // 3. 处理 ACK
    if (seg.header().ack) {
        _sender.ack_received(seg.header().ackno, seg.header().win);
    }

    // 4. 被动关闭优化
    if (_receiver.stream_out().input_ended() && !_sender.stream_in().eof()) {
        _linger_after_streams_finish = false;
    }

    // 5. 准备回复 ACK
    bool incoming_occupied_seqno = seg.length_in_sequence_space() > 0;

    bool is_keep_alive = false;
    if (_receiver.ackno().has_value() && 
        seg.length_in_sequence_space() == 0 && 
        seg.header().seqno == _receiver.ackno().value() - 1) {
        is_keep_alive = true;
    }

    // 只有在非 LISTEN 状态 (即 ackno 有值) 才填充窗口，防止意外发送 SYN
    if (_receiver.ackno().has_value()) {
        _sender.fill_window();
    }

    // 强制回复 Logic
    if (incoming_occupied_seqno || is_keep_alive || (!accept_success && _receiver.ackno().has_value())) {
        if (_sender.segments_out().empty()) {
            _sender.send_empty_segment();
        }
    }

    _trans_segments();
    _check_clean_shutdown();
}

bool TCPConnection::active() const { return _is_active; }

size_t TCPConnection::write(const string &data) {
    if (data.empty()) return 0;
    
    size_t written = _sender.stream_in().write(data);
    
    _sender.fill_window();
    _trans_segments();
    
    return written;
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) { 
    if (!_is_active) return;

    _time_since_last_segment_received += ms_since_last_tick;

    // 1. 告诉 Sender 时间流逝
    _sender.tick(ms_since_last_tick);

    // 2. 检查重传次数是否超标 -> 暴力退出
    if (_sender.consecutive_retransmissions() > TCPConfig::MAX_RETX_ATTEMPTS) {
        // 清除队列
        while(!_segments_out.empty()) _segments_out.pop();
        
        // 发送 RST
        TCPSegment rst_seg;
        rst_seg.header().rst = true;
        if (_receiver.ackno().has_value()) {
            rst_seg.header().ack = true;
            rst_seg.header().ackno = _receiver.ackno().value();
        }
        _segments_out.push(rst_seg);

        // 设置错误状态
        _receiver.stream_out().set_error();
        _sender.stream_in().set_error();
        _is_active = false;
        return;
    }

    // 转发可能产生的重传包
    _trans_segments();

    // 3. 检查是否可以结束连接
    _check_clean_shutdown();
}

void TCPConnection::end_input_stream() {
    _sender.stream_in().end_input();
    _sender.fill_window();
    _trans_segments();
}

void TCPConnection::connect() {
    _sender.fill_window(); // 生成 SYN
    _is_active = true;
    _trans_segments();
}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";

            // 发送 RST
            _sender.stream_in().set_error();
            _receiver.stream_out().set_error();
            _is_active = false;

            if (_sender.segments_out().empty()) {
                 _sender.send_empty_segment();
            }
             // 强制将 RST 标志位置为 true
            TCPSegment rst_seg;
            if (_sender.segments_out().empty()) {
                // 如果为空，创建一个新的
                if (_receiver.ackno().has_value()) {
                    rst_seg.header().ack = true;
                    rst_seg.header().ackno = _receiver.ackno().value();
                }
            } else {
                // 如果队列里有包，取出来打上 RST
                rst_seg = _sender.segments_out().front();
                _sender.segments_out().pop();
            }
            
            rst_seg.header().rst = true;
            _segments_out.push(rst_seg);
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}