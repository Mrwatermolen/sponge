#include "tcp_connection.hh"

#include <iostream>
#include <limits>

// Dummy implementation of a TCP connection

// For Lab 4, please replace with a real implementation that passes the
// automated checks run by `make check`.

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

size_t TCPConnection::remaining_outbound_capacity() const { return _sender.stream_in().remaining_capacity(); }

size_t TCPConnection::bytes_in_flight() const { return _sender.bytes_in_flight(); }

size_t TCPConnection::unassembled_bytes() const { return _receiver.unassembled_bytes(); }

size_t TCPConnection::time_since_last_segment_received() const { return _time_pass - _last_received_time; }

void TCPConnection::segment_received(const TCPSegment &seg) {
    cout << "TCPConnection::segment_received() Start." << endl;
    if (!_had_tried) {
        _actived = true;
        _had_tried = true;
    }
    const TCPHeader &header = seg.header();
    // cout << "T:" << header.summary() << endl << seg.length_in_sequence_space() << endl << endl;
    if (header.rst) {
        abort_connection();
        return;
    }

    // Receiving
    _receiver.segment_received(seg);
    auto ackno = _receiver.ackno();
    if (!ackno.has_value()) {
        // illegal seg
        return;
    }
    _last_received_time = _time_pass;

    // Sending
    _sender.ack_received(ackno.value(), header.win);
    if (seg.length_in_sequence_space() && _sender.segments_out().empty()) {
        // keep-alives
        _sender.send_empty_segment();
        // send_seg();
    }
    send_seg();
    cout << "TCPConnection::segment_received() End." << endl;
    cout << endl;
}

bool TCPConnection::active() const { return _actived; }

size_t TCPConnection::write(const string &data) { return _sender.stream_in().write(data); }

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) {
    cout << "TCPConnection::tick() Start." << endl;
    cur_state();
    _time_pass += ms_since_last_tick;
    _sender.tick(ms_since_last_tick);
    uint64_t expire_time = _last_received_time + 10 * _cfg.rt_timeout;

    if (_cfg.MAX_RETX_ATTEMPTS < _sender.consecutive_retransmissions()) {
        _sender.fill_window();
        send_seg();
        abort_connection();
        cout << "TCPConnection::tick() End." << endl;
        cout << endl;
        return;
    }

    bool prereq_1 = _receiver.unassembled_bytes() == 0 &&
                    _receiver.stream_out().eof();  // The inbound stream has been fully assembled and has ended.

    bool prereq_2 = _sender.sent_fin();
    bool prereq_3 =
        _sender.bytes_in_flight() == 0;  // The outbound stream has been fully acknowledged by the remote peer.
    if (prereq_2) {
        _linger_after_streams_finish = false;
    }

    if (!_linger_after_streams_finish && prereq_1 && prereq_3) {
        _actived = false;
        cur_state();
        cout << "TCPConnection::tick() End." << endl;
        cout << endl;
        return;
    }

    if (!prereq_1 || !prereq_3 || _time_pass < expire_time) {
        cur_state();
        cout << "TCPConnection::tick() End." << endl;
        cout << endl;
        return;
    }

    _actived = false;
    cout << "TCPConnection::tick() End." << endl;
    cout << endl;
}

void TCPConnection::end_input_stream() {
    cout << "TCPConnection::end_input_stream() Start." << endl;
    _sender.stream_in().end_input();
    _sender.fill_window();
    send_seg();
    cout << "TCPConnection::end_input_stream() End." << endl;
    cout << endl;
}

void TCPConnection::connect() {
    cout << "TCPConnection::connect() Start." << endl;
    if (!_had_tried) {
        _actived = true;
        _had_tried = true;
    }
    _sender.fill_window();
    send_seg();
    cout << "TCPConnection::connect() End." << endl;
    cout << endl;
}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";

            // Your code here: need to send a RST segment to the peer
            abort_connection();
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}

void TCPConnection::abort_connection() {
    cout << "TCPConnection::abort_connection() Start." << endl;
    cur_state();
    _sender.send_empty_segment_with_rst();
    send_seg();
    _sender.stream_in().error();
    _receiver.stream_out().error();
    _actived = false;
    cout << "TCPConnection::abort_connection() End." << endl;
    cout << endl;
}

void TCPConnection::send_seg() {
    cout << "TCPConnection::send_seg() Start." << endl;
    cur_state();
    while (!_sender.segments_out().empty()) {
        TCPSegment seg = _sender.segments_out().front();
        if (_receiver.ackno().has_value()) {
            // cout << _receiver.ackno().value() << endl;
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
        cout << seg.header().summary() << endl;
    }
    cout << "TCPConnection::send_seg() End." << endl;
}

void TCPConnection::cur_state() {
    TCPSegment seg;
    if (_receiver.ackno().has_value()) {
        seg.header().ackno = _receiver.ackno().value();
    }
    seg.header().fin = _sender.sent_fin();
    seg.header().seqno = _sender.next_seqno();

    cout << "Currant State:"
         << "linger: " << _linger_after_streams_finish << ". actived: " << _actived
         << ". segments_out: " << _segments_out.size() << ". " << seg.header().summary() << ". time pass:" << _time_pass
         << endl;
}