#include "byte_stream.hh"

// Dummy implementation of a flow-controlled in-memory byte stream.

// For Lab 0, please replace with a real implementation that passes the
// automated checks run by `make check_lab0`.

// You will need to add private members to the class declaration in `byte_stream.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

ByteStream::ByteStream(const size_t capacity)
        : _capacity(capacity), _buffer(), _bytes_written(0), _bytes_read(0),
            _end_input(false), _error(false) {}

size_t ByteStream::write(const string &data) {
    if (_end_input) return 0; // no more writes accepted after end_input
    size_t can_write = remaining_capacity();
    size_t to_write = min(can_write, data.size());
    if (to_write > 0) {
        _buffer.append(data.data(), to_write);
        _bytes_written += to_write;
    }
    return to_write;
}

//! \param[in] len bytes will be copied from the output side of the buffer
string ByteStream::peek_output(const size_t len) const {
    size_t n = min(len, _buffer.size());
    return _buffer.substr(0, n);
}

//! \param[in] len bytes will be removed from the output side of the buffer
void ByteStream::pop_output(const size_t len) {
    size_t n = min(len, _buffer.size());
    // remove first n bytes
    if (n == 0) return;
    _buffer.erase(0, n);
    _bytes_read += n;
}

//! Read (i.e., copy and then pop) the next "len" bytes of the stream
//! \param[in] len bytes will be popped and returned
//! \returns a string
std::string ByteStream::read(const size_t len) {
    string out = peek_output(len);
    pop_output(out.size());
    return out;
}

void ByteStream::end_input() { _end_input = true; }

bool ByteStream::input_ended() const { return _end_input; }

size_t ByteStream::buffer_size() const { return _buffer.size(); }

bool ByteStream::buffer_empty() const { return _buffer.empty(); }

bool ByteStream::eof() const { return _end_input && _buffer.empty(); }

size_t ByteStream::bytes_written() const { return _bytes_written; }

size_t ByteStream::bytes_read() const { return _bytes_read; }

size_t ByteStream::remaining_capacity() const {
    if (_capacity >= _buffer.size()) return _capacity - _buffer.size();
    return 0;
}
