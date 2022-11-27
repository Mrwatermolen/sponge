#include "tcp_receiver.hh"

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

using namespace std;

TCPReceiver::TCPReceiver(const size_t capacity)
    : _reassembler(capacity), _capacity(capacity), _state(TCPReceiverState::Listening), _isn(WrappingInt32(0)) {}

void TCPReceiver::segment_received(const TCPSegment &seg) {
    switch (_state) {
        case TCPReceiverState::Listening: {
            trans_receiving_state(seg);
            break;
        }
        case TCPReceiverState::Receiving: {
            handle_payload(seg);
            break;
        }
        case TCPReceiverState::CloseWait: {
            // TODO
            // I suppose its job isn't to manage the transitions.I may refactor this code in future.
            break;
        }
        default: {
            break;
        }
    }
}

optional<WrappingInt32> TCPReceiver::ackno() const {
    if (_state == TCPReceiverState::Listening) {
        //  the ISN hasn’t been set
        return std::nullopt;
    }
    // abs_seq includes Include SYN/FIN,
    if (_state == TCPReceiverState::CloseWait) {
        // Receiver gets Fin signal and receives all segments safely.
        // So ACK = abs_seq + 1
        return wrap(_reassembler.reassembled_bytes() + 2, _isn);
    }
    // ACK = abs_seqno = The last index of reassembled plus one;
    return wrap(_reassembler.reassembled_bytes() + 1, _isn);
}

size_t TCPReceiver::window_size() const {
    // That is not my original opinion. I don't think it's correct.
    // remain_bytes also needs to minu bytes that is unreassembled.
    size_t remain_bytes = _reassembler.stream_out().buffer_size();  // FIXME
    return _capacity - remain_bytes;
}

void TCPReceiver::trans_receiving_state(const TCPSegment &seg) {
    if (_state != TCPReceiverState::Listening || !seg.header().syn) {
        return;
    }
    // get SYN signal
    _state = TCPReceiverState::Receiving;
    _isn = seg.header().seqno;
    handle_payload(seg);
}

void TCPReceiver::handle_payload(const TCPSegment &seg) {
    // check state
    if (_state != TCPReceiverState::Receiving) {
        return;
    }

    const TCPHeader &header = seg.header();
    auto abs_seq = unwrap(header.seqno, _isn, _reassembler.reassembled_bytes());
    // stream_index omits the  SYN/FIM
    // Be careful the case which is SYN is true.
    // The segment with SYN is a empty patload segment,
    auto stream_index = abs_seq;

    if (abs_seq == 0 && !header.syn) {
        // That is wrong segment
        // Why do we need to check this case?
        return;
    }
    if (abs_seq != 0) {
        // No SYN
        stream_index = abs_seq - 1;
    }

    _reassembler.push_substring(seg.payload().copy(), stream_index, header.fin);
    if (_reassembler.stream_out().input_ended()) {
        _state = TCPReceiverState::CloseWait;
    }
}