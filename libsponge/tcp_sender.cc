#include "tcp_sender.hh"

#include "tcp_config.hh"
#include "tcp_segment.hh"

#include <algorithm>
#include <iostream>
#include <random>

// Dummy implementation of a TCP sender

// For Lab 3, please replace with a real implementation that passes the
// automated checks run by `make check_lab3`.

using namespace std;

//! \param[in] capacity the capacity of the outgoing byte stream
//! \param[in] retx_timeout the initial amount of time to wait before retransmitting the oldest outstanding segment
//! \param[in] fixed_isn the Initial Sequence Number to use, if set (otherwise uses a random ISN)
TCPSender::TCPSender(const size_t capacity, const uint16_t retx_timeout, const std::optional<WrappingInt32> fixed_isn)
    : _isn(fixed_isn.value_or(WrappingInt32{random_device()()}))
    , _initial_retransmission_timeout{retx_timeout}
    , _RTO(_initial_retransmission_timeout)
    , _stream(capacity) {}

uint64_t TCPSender::bytes_in_flight() const { return _next_seqno - _last_ack_received; }

void TCPSender::fill_window() {
    // by reading from the ByteStream, creating new TCP segments
    cout << "TCPSender::fill_window() Start." << endl;
    cur_state_to_srting();
    auto temp_size = _sender_window_size;
    if (_sender_window_size == 0) {
        // When filling window, treat a '0' window size as equal to '1' but don't back off RTO。
        // provoke the receiver into sending a new acknowledgment segment where it reveals that more space has opened up
        // in its window. Without this, the sender would never learn that it was allowed to start sending again.
        temp_size = 1;
    }

    while (_next_seqno - _last_ack_received < temp_size && !_get_fin) {
        auto outstanding_bytes = _next_seqno - _last_ack_received;
        TCPSegment seg;
        if (!_next_seqno) {
            seg.header().syn = true;
            ++outstanding_bytes;
        }
        uint64_t nums = min(TCPConfig::MAX_PAYLOAD_SIZE, (temp_size - outstanding_bytes));
        auto payload = Buffer(_stream.read(nums));
        if (_stream.eof()) {
            _get_fin = true;
        }

        seg.header().seqno = wrap(_next_seqno, _isn);
        seg.payload() = payload;
        if (!seg.length_in_sequence_space()) {
            break;
        }

        _next_seqno += seg.length_in_sequence_space();
        if (_get_fin) {
            if (_next_seqno - _last_ack_received < temp_size) {
                // space is available
                seg.header().fin = true;
                _next_seqno += 1;
                _send_fin = true;
            }
        }
        _segments_out.push(seg);
        _out_backup.push_back(make_pair(seg, _time_pass));
    }

    if (_get_fin && !_send_fin) {
        if (temp_size <= _next_seqno - _last_ack_received) {
            cur_state_to_srting();
            cout << "TCPSender:fill_window() End." << endl << endl;
            return;
        }
        TCPSegment fin_seg;
        fin_seg.header().seqno = wrap(_next_seqno, _isn);
        fin_seg.header().fin = true;
        _next_seqno += 1;
        _send_fin = true;
        _segments_out.push(fin_seg);
        _out_backup.push_back(make_pair(fin_seg, _time_pass));
    }
    cur_state_to_srting();
    cout << "TCPSender::fill_window() End." << endl << endl;
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
    cout << "TCPSender::ack_received() Start. ackno:" << ackno << ". window_size:" << window_size << endl;
    cur_state_to_srting();

    auto l = unwrap(ackno, _isn, _next_seqno);
    cout << "l:" << l << ". r:" << l + window_size << endl;
    if (_next_seqno < l) {
        cout << "F:ack_received() End. _next_seqno < l" << endl;
        return;
    }
    _sender_window_size = window_size;
    if (l <= _last_ack_received) {
        return;
    }
    _last_ack_received = l;
    _consecutive_retransmissions = 0;
    _RTO = _initial_retransmission_timeout;
    for (auto it = _out_backup.begin(); it != _out_backup.end(); it++) {
        cout << "Check: " << it->first.header().summary()
             << ". sq: " << unwrap(it->first.header().seqno, _isn, _next_seqno)
             << ". size:" << it->first.length_in_sequence_space() << ". Time:" << it->second << endl;
        if (l <= unwrap(it->first.header().seqno, _isn, _next_seqno) + it->first.length_in_sequence_space() - 1) {
            // restart the retransmission timer
            it->second = _time_pass;
            continue;
        }
        cout << "Clear: " << it->first.header().summary() << endl;
        _out_backup.pop_front();
    }

    fill_window();
    cout << "TCPSender::ack_received() End." << endl << endl;
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) {
    cout << "TCPSender::tick() Start." << ms_since_last_tick << endl;
    _time_pass += ms_since_last_tick;
    bool retry = false;
    auto temp = _RTO;
    for (auto it = _out_backup.begin(); it != _out_backup.end(); ++it) {
        cout << "Check: " << it->first.header().summary() << endl;
        cout << "Time Pass:" << _time_pass << " ms. it->second: " << it->second << " ms. RTO: " << temp << endl;
        cout << "Check case:"
             << "Time Pass:" << _time_pass << " ms. Timer: " << it->second + temp << " ms" << endl;
        if (retry) {
            it->second = _time_pass;
        }
        if (_time_pass < it->second + temp) {
            continue;
        }
        if (!retry) {
            // if (_next_seqno - _last_ack_received <= _sender_window_size) {
            //     // If the window size is nonzero
            //     _RTO <<= 1;
            // }
            if (_sender_window_size) {
                // If the window size is nonzero
                // Holy shit
                _RTO <<= 1;
                _consecutive_retransmissions++;
            }
        }
        retry = true;
        _segments_out.push(it->first);
        it->second = _time_pass;
        cout << "Retry: " << it->first.header().summary() << endl;
    }
    cout << "TCPSender::tick() End." << ms_since_last_tick << endl << endl;
}

unsigned int TCPSender::consecutive_retransmissions() const { return _consecutive_retransmissions; }

void TCPSender::send_empty_segment() {
    cout << "TCPSender::send_empty_segment() Start." << endl;
    TCPSegment seg;
    seg.header().seqno = wrap(_next_seqno, _isn);
    _segments_out.push(seg);
    cout << "TCPSender::send_empty_segment() Start." << endl;
}

void TCPSender::send_empty_segment_with_rst() {
    TCPSegment seg;
    seg.header().seqno = wrap(_next_seqno, _isn);
    seg.header().rst = true;
    _segments_out.push(seg);
}

void TCPSender::cur_state_to_srting() const {
    cout << "Currant State:"
         << " _isn: " << _isn << ". segments_queue.size: " << _segments_out.size()
         << ". _stream.buffer_size:" << _stream.buffer_size() << ". _next_seqno:" << _next_seqno
         << ". LAR:" << _last_ack_received << ". SWS:" << _sender_window_size
         << ". outing_queue: " << _out_backup.size() << ". timepass:" << _time_pass << ". eof:" << _stream.eof()
         << endl;
}