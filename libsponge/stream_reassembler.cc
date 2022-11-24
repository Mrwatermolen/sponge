#include "stream_reassembler.hh"

#include <cstddef>
#include <iostream>
#include <string>
#include <utility>

// Dummy implementation of a stream reassembler.

// For Lab 1, please replace with a real implementation that passes the
// automated checks run by `make check_lab1`.

// You will need to add private members to the class declaration in `stream_reassembler.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

StreamReassembler::StreamReassembler(const size_t capacity)
    : _output(capacity)
    , _capacity(capacity)
    , _reassembled_len(0)
    , _eof_index(-1)
    , _unreassembled_data(map<size_t, std::string>()){};

void StreamReassembler::meger_str(const string &data, const size_t index) {
    // [l, r)
    size_t l = index, r = l + data.size();
    if (r <= l) {
        return;
    }
    if (_unreassembled_data.count(l) && _unreassembled_data[l].size() < r - l) {
        _unreassembled_data[l] = data;
    }
    _unreassembled_data.emplace(make_pair(l, data));

    // ordered
    auto i = _unreassembled_data.begin();
    auto j = i;
    ++i;
    while (true) {
        if (i == _unreassembled_data.end()) {
            return;
        }
        if (j->first + j->second.size() < i->first) {
            j = i;
            i++;
            continue;
        }
        if (j->first + j->second.size() < i->first + i->second.size()) {
            j->second = (j->second.substr(0, i->first - j->first)) + i->second;
        }
        i = _unreassembled_data.erase(i);
    }
}

void StreamReassembler::write_data_to_byte_stream() {
    for (auto i = _unreassembled_data.begin(); i != _unreassembled_data.end();) {
        if (_output.bytes_written() != i->first) {
            return;
        }

        size_t write_begin = i->first;
        auto write_size = _output.write(i->second);
        if (write_size == 0) {
            return;
        }
        _reassembled_len = _output.bytes_written();
        if (write_size < i->second.size()) {
            _unreassembled_data.emplace(
                make_pair(write_begin + write_size, i->second.substr(write_size, i->second.size() - write_size)));
        }
        i = _unreassembled_data.erase(i);
        if (_unreassembled_data.empty()) {
            return;
        }
    }
}

//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.
void StreamReassembler::push_substring(const string &data, const size_t index, const bool eof) {
    DUMMY_CODE(data, index, eof);
    if (_eof_index == _reassembled_len) {
        return;
    }

    string s = data;
    size_t i = index;

    if (eof) {
        _eof_index = i + s.size();
    }

    // check deuplicate
    if (i < _reassembled_len) {
        if (i + s.size() <= _reassembled_len) {
            return;
        }

        s = s.substr(_reassembled_len - index, s.size() - _reassembled_len + i);
        i = _reassembled_len;
    }

    // flow control
    size_t r = 0;
    if (!_unreassembled_data.empty()) {
        auto last = _unreassembled_data.end();
        last--;
        r = last->first + last->second.size();
    }
    if (r < i + s.size()) {
        size_t unread_bytes = _reassembled_len - _output.bytes_read();
        size_t unreassembled_bytes = r - _reassembled_len;
        size_t total_space = unread_bytes + unreassembled_bytes + (i + s.size() - r);
        if (total_space > _capacity) {
            size_t remain = _capacity - (unread_bytes + unreassembled_bytes);
            s = s.substr(0, unreassembled_bytes + remain);
        }
    }

    meger_str(s, i);

    write_data_to_byte_stream();

    if (_eof_index == _reassembled_len) {
        _output.end_input();
    }
}

size_t StreamReassembler::unassembled_bytes() const {
    size_t counter = 0;
    for (auto &&i : _unreassembled_data) {
        counter += i.second.size();
    }
    return counter;
}

bool StreamReassembler::empty() const { return _unreassembled_data.empty(); }
