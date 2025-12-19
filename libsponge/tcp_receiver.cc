#include "tcp_receiver.hh"

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

bool TCPReceiver::segment_received(const TCPSegment &seg) {
    const TCPHeader &header = seg.header();

    // 1. 判断当前是否为 Listen 状态 (未正式建立连接，需要 SYN 启动)
    //    我们将 _isn 是否有值作为是否收到过 SYN 的标志
    if (!_isn.has_value()) {
        // 如果是 SYN 包，则提取 ISN 并设置状态
        if (header.syn) {
            _isn = header.seqno;
        } else {
            // 否，则数据传输未开始，丢弃数据包
            return false;
        }
    }

    // 2. 将数据包中 payload 的数据放进流重组器
    //    我们需要计算“流索引 (Stream Index)”
    
    // a. 获取 checkpoint：这是重组器中“下一个期望写入的字节索引”
    //    用作 unwrap 的参考点，可以防止序列号回绕导致的歧义
    uint64_t checkpoint = _reassembler.stream_out().bytes_written();

    // b. 计算绝对序列号 (Absolute Sequence Number)
    uint64_t abs_seqno = unwrap(header.seqno, _isn.value(), checkpoint);

    // c. 计算流索引 (Stream Index)
    uint64_t stream_idx = abs_seqno - 1;
    
    // 如果是 SYN 包，abs_seqno 为 0，直接减 1 会下溢出。
    // 但逻辑上 SYN 包携带的数据（如果有）应该从流的第 0 个字节开始。
    if (header.syn) {
        stream_idx = 0;
    }

    // d. 调用 push_substring
    _reassembler.push_substring(seg.payload().copy(), stream_idx, header.fin);
    return true;
}

optional<WrappingInt32> TCPReceiver::ackno() const { 
    // 判断当前是否为 Listen 状态 (即还没有收到 SYN)
    if (!_isn.has_value()) {
        return std::nullopt;
    }

    // 计算下一个期望的绝对序列号 (Absolute Seqno)
    // 基础：已写入字节流的字节数 + 1 (因为 SYN 占用了 1 个序列号)
    uint64_t abs_ack_no = _reassembler.stream_out().bytes_written() + 1;

    // !!! 注意：
    // 如果当前处于 FIN_RECV 状态 (流已结束)，则还需要加上 FIN 标志长度 (1个序列号)
    // stream_out().input_ended() 为真意味着重组器已经收到了 FIN 并且所有数据都已重组完毕
    if (_reassembler.stream_out().input_ended()) {
        abs_ack_no += 1;
    }

    // 将绝对序列号转换回 32 位序列号
    return wrap(abs_ack_no, _isn.value());
}

size_t TCPReceiver::window_size() const { 
    // 计算总容量 _capacity 与流重组器中已存数据量 ( ByteStream buffer ) 的差值
    // stream_out().buffer_size() 返回的是当前 ByteStream 中尚未被读取的数据量
    return _capacity - _reassembler.stream_out().buffer_size();
}
