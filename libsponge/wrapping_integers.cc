#include "wrapping_integers.hh"

// Dummy implementation of a 32-bit wrapping integer

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! Transform an "absolute" 64-bit sequence number (zero-indexed) into a WrappingInt32
//! \param n The input absolute 64-bit sequence number
//! \param isn The initial sequence number
WrappingInt32 wrap(uint64_t n, WrappingInt32 isn) {
    // 将 64 位绝对序列号转换为 32 位（高位自动截断，相当于 mod 2^32）
    // 然后加上初始序列号 ISN
    return WrappingInt32{isn.raw_value() + static_cast<uint32_t>(n)};
}

//! Transform a WrappingInt32 into an "absolute" 64-bit sequence number (zero-indexed)
//! \param n The relative sequence number
//! \param isn The initial sequence number
//! \param checkpoint A recent absolute 64-bit sequence number
//! \returns the 64-bit sequence number that wraps to `n` and is closest to `checkpoint`
//!
//! \note Each of the two streams of the TCP connection has its own ISN. One stream
//! runs from the local TCPSender to the remote TCPReceiver and has one ISN,
//! and the other stream runs from the remote TCPSender to the local TCPReceiver and
//! has a different ISN.
uint64_t unwrap(WrappingInt32 n, WrappingInt32 isn, uint64_t checkpoint) {
    // 1. 计算 checkpoint 对应的 WrappingInt32 值
    WrappingInt32 ckpt_wrapped = wrap(checkpoint, isn);

    // 2. 计算 n 与 ckpt_wrapped 之间的有符号距离 (int32_t)
    // helper 函数 operator- 会自动处理环上的最短距离，结果范围在 [-2^31, 2^31-1]
    int32_t diff = n - ckpt_wrapped;

    // 3. 将距离加到绝对 checkpoint 上，得到理论上最近的绝对序列号
    int64_t res = static_cast<int64_t>(checkpoint) + diff;

    // 4. 边界处理：绝对序列号不能为负数
    // 如果结果为负（说明 checkpoint 接近 0 且 n 在 checkpoint 的"左边"很远），
    // 我们需要加上 2^32 (1ULL << 32) 来获取下一个合法的正数值
    if (res < 0) {
        return static_cast<uint64_t>(res + (1ULL << 32));
    }

    return static_cast<uint64_t>(res);
}
