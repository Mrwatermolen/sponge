#include "tcp_connection.hh"

#include <limits>

// Dummy implementation of a TCP connection

// For Lab 4, please replace with a real implementation that passes the
// automated checks run by `make check`.

using namespace std;

size_t TCPConnection::remaining_outbound_capacity() const { return _sender.stream_in().remaining_capacity(); }

size_t TCPConnection::bytes_in_flight() const { return _sender.bytes_in_flight(); }

size_t TCPConnection::unassembled_bytes() const { return _receiver.unassembled_bytes(); }

size_t TCPConnection::time_since_last_segment_received() const { return _time_pass - _last_received_time; }

void TCPConnection::segment_received(const TCPSegment &seg) {
    const TCPHeader &header{seg.header()};
    // Receiving
    _receiver.segment_received(seg);

    // Flag rst is ingored when the state of reciever is LISTEN.
    // So check flag rst after receiving.
    if (header.rst) {
        // dosen't need to send seg with flag rst.
        abort_connection(false);
        return;
    }

    _last_received_time = _time_pass;

    // Sending
    if (header.ack) {
        _sender.ack_received(header.ackno, header.win);
    }

    if (TCPState::state_summary(_receiver) == TCPReceiverStateSummary::SYN_RECV &&
        TCPState::state_summary(_sender) == TCPSenderStateSummary::CLOSED) {
        // Send SYN and ACK
        connect();
        return;
    }

    // if (!_receiver.ackno().has_value()) {
    //     return;
    // }

    if (TCPState::state_summary(_receiver) == TCPReceiverStateSummary::FIN_RECV &&
        TCPState::state_summary(_sender) == TCPSenderStateSummary::SYN_ACKED) {
        // The TCPConnection’s inbound stream ends before the TCPConnection has ever sent a fin segment.
        // doesn’t need to linger after both streams finish.
        _linger_after_streams_finish = false;
    }

    // check if both streams finish when linger is false.
    if (!_linger_after_streams_finish && TCPState::state_summary(_receiver) == TCPReceiverStateSummary::FIN_RECV &&
        TCPState::state_summary(_sender) == TCPSenderStateSummary::FIN_ACKED) {
        // passive close.
        _actived = false;
        return;
    }

    // if (seg.length_in_sequence_space() == 0 && _receiver.ackno().value() - 1 == seg.header().seqno) {
    //     // keep-alives
    //     _sender.send_empty_segment();
    //     send_seg();
    //     return;
    // }

    if (seg.length_in_sequence_space() && _sender.segments_out().empty()) {
        // send a seg to keep alives.
        _sender.send_empty_segment();
    }

    send_seg();
}

bool TCPConnection::active() const { return _actived; }

size_t TCPConnection::write(const string &data) {
    auto size{_sender.stream_in().write(data)};
    _sender.fill_window();
    send_seg();
    return size;
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) {
    _time_pass += ms_since_last_tick;
    _sender.tick(ms_since_last_tick);

    if (_cfg.MAX_RETX_ATTEMPTS < _sender.consecutive_retransmissions()) {
        // need to send seg with flag rst
        abort_connection(true);
        return;
    }

    send_seg();

    try_active_close();
}

void TCPConnection::end_input_stream() {
    _sender.stream_in().end_input();
    _sender.fill_window();
    send_seg();
}

void TCPConnection::connect() {
    _actived = true;
    _sender.fill_window();
    send_seg();
}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";

            // Your code here: need to send a RST segment to the peer
            // ? need to send a RST segment ?
            // Test say NO
            abort_connection(false);
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}

void TCPConnection::abort_connection(bool send_rst) {
    while (!_sender.segments_out().empty()) {
        _sender.segments_out().pop();
    }
    _sender.stream_in().set_error();
    _receiver.stream_out().set_error();
    _actived = false;
    if (send_rst) {
        _sender.send_empty_segment_with_rst();
        send_seg();
    }
}

void TCPConnection::send_seg() {
    while (!_sender.segments_out().empty()) {
        TCPSegment seg = _sender.segments_out().front();
        if (_receiver.ackno().has_value()) {
            seg.header().ackno = _receiver.ackno().value();
            if (std::numeric_limits<uint16_t>::max() < _receiver.window_size()) {
                seg.header().win = std::numeric_limits<uint16_t>::max();
            } else {
                seg.header().win = _receiver.window_size();
            }
            seg.header().ack = true;
        }

        _segments_out.push(seg);
        _sender.segments_out().pop();
    }
}

void TCPConnection::try_active_close() {
    if (!_linger_after_streams_finish) {
        return;
    }
    // check pre #1: The inbound stream has been fully assembled and has ended.
    // check pre #3: The outbound stream has been fully acknowledged by the remote peer.

    if (TCPState::state_summary(_receiver) != TCPReceiverStateSummary::FIN_RECV ||
        TCPState::state_summary(_sender) != TCPSenderStateSummary::FIN_ACKED) {
        return;
    }

    auto expire_time{_last_received_time + 10 * _cfg.rt_timeout};

    if (_time_pass < expire_time) {
        return;
    }

    _actived = false;
}