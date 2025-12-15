#include "tcp_sender.hh"

#include "tcp_config.hh"

#include <algorithm>
#include <random>

using namespace std;

TCPSender::TCPSender(const size_t capacity, const uint16_t retx_timeout, const std::optional<WrappingInt32> fixed_isn)
    : _isn(fixed_isn.value_or(WrappingInt32{random_device()()}))
    , _initial_retransmission_timeout{retx_timeout}
    , _stream(capacity)
    , _current_rto(retx_timeout) {}

uint64_t TCPSender::bytes_in_flight() const { return _bytes_in_flight; }

void TCPSender::fill_window() {
    // 1. 获取当前有效窗口大小 (处理 0 窗口探测)
    size_t current_window = (_current_window_size == 0) ? 1 : _current_window_size;

    // 2. 循环填充窗口
    while (current_window > _bytes_in_flight) {
        TCPSegment seg;
        
        // 计算当前还可以发送多少字节 (Sequnce Space)
        // 标准公式：limit = window - (next - recv)
        size_t window_remaining = current_window - (_next_seqno - _recv_ackno);
        
        // 如果窗口已满，退出循环
        if (window_remaining == 0) {
            break;
        }

        // A. 处理 SYN
        if (_next_seqno == 0) {
            seg.header().syn = true;
            window_remaining--;
        }

        // B. 处理 Payload
        // 允许的最大 payload 是: min(TCP限制, 窗口剩余空间)
        size_t payload_size = min(TCPConfig::MAX_PAYLOAD_SIZE, window_remaining);
        
        // 从流中读取数据
        string payload = _stream.read(payload_size);
        seg.payload() = Buffer(std::move(payload));
        
        // 更新窗口剩余空间
        window_remaining -= seg.payload().size();

        // C. 处理 FIN
        if (_stream.eof() && window_remaining > 0) {
            // 检查是否已经发送过 FIN (防止重发)
            // 此时 seg 还没加 FIN，所以它的长度是 SYN + Payload
            size_t len_so_far = seg.length_in_sequence_space();
            
            // 绝对序列号空间中，数据的最后一个字节是 SYN(1) + bytes_written
            // FIN 的位置是 SYN(1) + bytes_written + 1
            if (_next_seqno + len_so_far == _stream.bytes_written() + 1) {
                seg.header().fin = true;
                // window_remaining--; // 不需要再减了，反正要 break 了
            }
        }

        // D. 发送检查
        size_t seg_length = seg.length_in_sequence_space();
        
        // 如果段长度为 0，说明无事可做，退出
        if (seg_length == 0) {
            break;
        }

        // 设置序列号
        seg.header().seqno = wrap(_next_seqno, _isn);

        // 发送并追踪
        _segments_out.push(seg);
        _outstanding_segments.push(seg);

        // 更新状态
        _next_seqno += seg_length;
        _bytes_in_flight += seg_length;

        // 启动计时器
        if (!_timer_running) {
            _timer_running = true;
            _timer_ms = 0;
        }

        // 如果发送了 FIN，流结束，不再发送
        if (seg.header().fin) {
            break;
        }
    }
}

void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
    uint64_t abs_ackno = unwrap(ackno, _isn, _next_seqno);

    // 1. 检查 ack 是否非法 (确认了未来的数据)
    if (abs_ackno > _next_seqno) {
        return;
    }

    // 2. 更新窗口
    _current_window_size = window_size;

    // 3. 检查是否是旧 ACK (已被处理过)
    if (abs_ackno <= _recv_ackno) {
        return;
    }

    // 4. 推进 ACK 并处理重传队列
    _recv_ackno = abs_ackno;

    bool new_data_acked = false;
    while (!_outstanding_segments.empty()) {
        TCPSegment &seg = _outstanding_segments.front();
        
        uint64_t seg_abs_seqno = unwrap(seg.header().seqno, _isn, _next_seqno);
        uint64_t seg_length = seg.length_in_sequence_space();

        // 检查该段是否被完全确认
        if (seg_abs_seqno + seg_length <= abs_ackno) {
            _bytes_in_flight -= seg_length;
            _outstanding_segments.pop();
            new_data_acked = true;
        } else {
            break;
        }
    }

    // 5. 重置计时器逻辑
    if (new_data_acked) {
        _current_rto = _initial_retransmission_timeout;
        _consecutive_retransmissions = 0;
        _timer_ms = 0;
        _timer_running = !_outstanding_segments.empty();
    }
    
    // 6. 窗口更新后，再次尝试填充窗口
    fill_window();
}

void TCPSender::tick(const size_t ms_since_last_tick) {
    if (!_timer_running) {
        return;
    }

    _timer_ms += ms_since_last_tick;

    // 超时处理
    if (_timer_ms >= _current_rto && !_outstanding_segments.empty()) {
        // 重传最早的段
        _segments_out.push(_outstanding_segments.front());

        // 拥塞控制：窗口非 0 时才退避
        if (_current_window_size != 0) {
            _consecutive_retransmissions++;
            _current_rto *= 2;
        }

        _timer_ms = 0;
    }
    
    if (_outstanding_segments.empty()) {
        _timer_running = false;
    }
}

unsigned int TCPSender::consecutive_retransmissions() const { 
    return _consecutive_retransmissions; 
}

void TCPSender::send_empty_segment() {
    TCPSegment seg;
    seg.header().seqno = wrap(_next_seqno, _isn);
    _segments_out.push(seg);
}