#include "wrapping_integers.hh"

// Dummy implementation of a 32-bit wrapping integer

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

//! Transform an "absolute" 64-bit sequence number (zero-indexed) into a WrappingInt32
//! \param n The input absolute 64-bit sequence number
//! \param isn The initial sequence number
WrappingInt32 wrap(uint64_t n, WrappingInt32 isn) {
    DUMMY_CODE(n, isn);
    uint64_t base = uint64_t(UINT32_MAX) + 1;
    uint32_t seqno = uint32_t((n + (isn.raw_value())) % base);
    return WrappingInt32{seqno};
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
    DUMMY_CODE(n, isn, checkpoint);

    WrappingInt32 &&c_seqno = wrap(checkpoint, isn);
    uint64_t n_abs_seqno = 0;
    auto offset = n - c_seqno;
    auto abs_offset = uint64_t(abs(offset));
    if (checkpoint < abs_offset) {
        n_abs_seqno = uint32_t(checkpoint) + offset;
    } else {
        n_abs_seqno = checkpoint + offset;
    }

    return n_abs_seqno;
}
