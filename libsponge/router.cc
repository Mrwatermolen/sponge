#include "router.hh"

#include "address.hh"

#include <bits/stdint-uintn.h>
#include <iostream>

using namespace std;

// Dummy implementation of an IP router

// Given an incoming Internet datagram, the router decides
// (1) which interface to send it out on, and
// (2) what next hop address to send it to.

// For Lab 6, please replace with a real implementation that passes the
// automated checks run by `make check_lab6`.

// You will need to add private members to the class declaration in `router.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

//! \param[in] route_prefix The "up-to-32-bit" IPv4 address prefix to match the datagram's destination address against
//! \param[in] prefix_length For this route to be applicable, how many high-order (most-significant) bits of the route_prefix will need to match the corresponding bits of the datagram's destination address?
//! \param[in] next_hop The IP address of the next hop. Will be empty if the network is directly attached to the router (in which case, the next hop address should be the datagram's final destination).
//! \param[in] interface_num The index of the interface to send the datagram out on.
void Router::add_route(const uint32_t route_prefix,
                       const uint8_t prefix_length,
                       const optional<Address> next_hop,
                       const size_t interface_num) {
    cerr << "DEBUG: adding route " << Address::from_ipv4_numeric(route_prefix).ip() << "/" << int(prefix_length)
         << " => " << (next_hop.has_value() ? next_hop->ip() : "(direct)") << " on interface " << interface_num << "\n";

    auto &elment{_forward_table[prefix_length]};
    if (auto entry{elment.find(route_prefix)}; entry == elment.end()) {
        auto pair{make_pair(route_prefix, RouterEntry{interface_num, next_hop})};
        elment.emplace(pair);
    } else {
        entry->second.interface_num = interface_num;
        entry->second.next_hop = next_hop;
    }
}

//! \param[in] dgram The datagram to be routed
void Router::route_one_datagram(InternetDatagram &dgram) {
    if (!dgram.header().ttl || !--dgram.header().ttl) {
        return;
    }
    auto dst_ip{dgram.header().dst};
    constexpr uint8_t max_len{32};
    for (uint8_t i{0}; i <= max_len; ++i) {
        auto &elment = _forward_table[max_len - i];
        if (elment.empty()) {
            continue;
        }
        auto prefix_length{max_len - i};
        if (!prefix_length) {
            auto entry{elment.begin()};
            auto next_hop{(entry->second.next_hop.has_value()) ? (entry->second.next_hop.value())
                                                               : (Address::from_ipv4_numeric(dst_ip))};
            interface(entry->first).send_datagram(dgram, next_hop);
            return;
        }

        uint32_t mask{std::numeric_limits<uint32_t>::max()};
        mask <<= (max_len - prefix_length);
        auto to_macth_ip{dst_ip & mask};
        if (!elment.count(to_macth_ip)) {
            continue;
        }

        auto entry{elment.find(to_macth_ip)};
        auto next_hop{(entry->second.next_hop.has_value()) ? (entry->second.next_hop.value())
                                                           : (Address::from_ipv4_numeric(dst_ip))};
        interface(entry->second.interface_num).send_datagram(dgram, next_hop);
        return;
    }
}

void Router::route() {
    // Go through all the interfaces, and route every incoming datagram to its proper outgoing interface.
    for (auto &interface : _interfaces) {
        auto &queue = interface.datagrams_out();
        while (not queue.empty()) {
            route_one_datagram(queue.front());
            queue.pop();
        }
    }
}
