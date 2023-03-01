#include "tcp_receiver.hh"

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

using namespace std;

TCPReceiver::TCPReceiver(const size_t capacity) : _reassembler(capacity), _capacity(capacity) {}

void TCPReceiver::segment_received(const TCPSegment &seg) {
    const auto &header{seg.header()};
    if (!_syn_flag) {
        if (!header.syn) {
            return;
        }
        _isn = header.seqno;
        _syn_flag = true;
    }

    auto abs_seq = unwrap(header.seqno, _isn, _reassembler.reassembled_bytes());
    // stream_index omits the  SYN/FIM
    // Be careful the case which is SYN is true.
    // The segment with SYN is a empty patload segment,
    auto stream_index = abs_seq;

    // if (abs_seq == 0 && !header.syn) {
    //     // That is wrong segment
    //     // Why do we need to check this case?
    //     return;
    // }
    stream_index -= !header.syn;

    _reassembler.push_substring(seg.payload().copy(), stream_index, header.fin);
}

optional<WrappingInt32> TCPReceiver::ackno() const {
    if (!_syn_flag) {
        return std::nullopt;
    }
    if (_reassembler.stream_out().input_ended()) {
        return wrap(_reassembler.reassembled_bytes() + 2, _isn);
    }
    return wrap(_reassembler.reassembled_bytes() + 1, _isn);
}

size_t TCPReceiver::window_size() const {
    auto remain_bytes{_reassembler.stream_out().buffer_size()};
    return _capacity - remain_bytes;
}