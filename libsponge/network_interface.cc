#include "network_interface.hh"

#include "arp_message.hh"
#include "ethernet_frame.hh"

#include <iostream>

// Dummy implementation of a network interface
// Translates from {IP datagram, next hop address} to link-layer frame, and from link-layer frame to IP datagram

// For Lab 5, please replace with a real implementation that passes the
// automated checks run by `make check_lab5`.

// You will need to add private members to the class declaration in `network_interface.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

//! \param[in] ethernet_address Ethernet (what ARP calls "hardware") address of the interface
//! \param[in] ip_address IP (what ARP calls "protocol") address of the interface
NetworkInterface::NetworkInterface(const EthernetAddress &ethernet_address, const Address &ip_address)
    : _ethernet_address(ethernet_address), _ip_address(ip_address) {
    cerr << "DEBUG: Network interface has Ethernet address " << to_string(_ethernet_address) << " and IP address "
         << ip_address.ip() << "\n";
}

//! \param[in] dgram the IPv4 datagram to be sent
//! \param[in] next_hop the IP address of the interface to send it to (typically a router or default gateway, but may also be another host if directly connected to the same network as the destination)
//! (Note: the Address type can be converted to a uint32_t (raw 32-bit IP address) with the Address::ipv4_numeric() method.)
void NetworkInterface::send_datagram(const InternetDatagram &dgram, const Address &next_hop) {
    // convert IP address of next hop to raw 32-bit representation (used in ARP header)
    const uint32_t next_hop_ip = next_hop.ipv4_numeric();
    if (!_address_cache.count(next_hop_ip)) {
        // call ARP
        broadcastReqeustARP(next_hop_ip);
        if (!_unkonw_ip_datagarm.count(next_hop_ip)) {
            auto unsend_queue{queue<InternetDatagram>()};
            _unkonw_ip_datagarm.emplace(make_pair(next_hop_ip, unsend_queue));
        }

        _unkonw_ip_datagarm.find(next_hop_ip)->second.emplace(dgram);
        return;
    }

    auto ehter_dst{_address_cache[next_hop_ip].address};
    auto frame{EthernetFrame{}};
    auto &header{frame.header()};
    header.type = EthernetHeader::TYPE_IPv4;
    header.src = _ethernet_address;
    header.dst = ehter_dst;
    frame.payload() = dgram.serialize();
    _frames_out.emplace(frame);
    // cout << "NetworkInterface::send_datagram() End" << endl;
}

//! \param[in] frame the incoming Ethernet frame
optional<InternetDatagram> NetworkInterface::recv_frame(const EthernetFrame &frame) {
    auto header{frame.header()};
    if (header.dst != ETHERNET_BROADCAST && header.dst != _ethernet_address) {
        return {};
    }
    if (header.type == EthernetHeader::TYPE_IPv4) {
        // cout << "NetworkInterface::recv_frame() TYPE_IPv4" << endl;
        auto datagram{InternetDatagram{}};
        if (datagram.parse(frame.payload()) != ParseResult::NoError) {
            return {};
        }

        return datagram;
    }
    if (header.type == EthernetHeader::TYPE_ARP) {
        auto mes{ARPMessage{}};
        if (mes.parse(frame.payload()) != ParseResult::NoError) {
            return {};
        }
        learnMappingFromARP(mes);
        return {};
    }
    return {};
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick(const size_t ms_since_last_tick) {
    _time_pass += ms_since_last_tick;
    auto i{_ARP_request_cache.begin()};
    while (i != _ARP_request_cache.end()) {
        if (_time_pass < i->second) {
            ++i;
            continue;
        }

        i = _ARP_request_cache.erase(i);
    }
    auto j{_address_cache.begin()};
    while (j != _address_cache.end()) {
        if (_time_pass < j->second.expire_time) {
            ++j;
            continue;
        }

        j = _address_cache.erase(j);
    }
}

void NetworkInterface::broadcastReqeustARP(const uint32_t &ip) {
    if (_ARP_request_cache.count(ip)) {
        return;
    }

    auto mes{ARPMessage()};
    mes.opcode = ARPMessage::OPCODE_REQUEST;
    mes.sender_ethernet_address = _ethernet_address;
    mes.sender_ip_address = _ip_address.ipv4_numeric();
    mes.target_ip_address = ip;
    auto frame{EthernetFrame{}};
    frame.header().type = EthernetHeader::TYPE_ARP;
    frame.header().src = _ethernet_address;
    frame.header().dst = ETHERNET_BROADCAST;
    frame.payload() = mes.serialize();
    _frames_out.emplace(frame);

    _ARP_request_cache.emplace(make_pair(ip, _time_pass + 5000));
}

void NetworkInterface::learnMappingFromARP(const ARPMessage &mes) {
    if (mes.opcode == ARPMessage::OPCODE_REQUEST && mes.target_ip_address == _ip_address.ipv4_numeric()) {
        auto reply_msg{ARPMessage()};
        reply_msg.opcode = ARPMessage::OPCODE_REPLY;
        reply_msg.sender_ethernet_address = _ethernet_address;
        reply_msg.sender_ip_address = _ip_address.ipv4_numeric();
        reply_msg.target_ethernet_address = mes.sender_ethernet_address;
        reply_msg.target_ip_address = mes.sender_ip_address;
        auto frame{EthernetFrame{}};
        frame.header().type = EthernetHeader::TYPE_ARP;
        frame.header().src = _ethernet_address;
        frame.header().dst = mes.sender_ethernet_address;
        frame.payload() = reply_msg.serialize();
        _frames_out.emplace(frame);
    }

    if (!_address_cache.count(mes.sender_ip_address)) {
        auto ethernet_timer{EhternetAdressTimer{mes.sender_ethernet_address, _time_pass + 30000}};
        _address_cache.emplace(make_pair(mes.sender_ip_address, ethernet_timer));
    }

    auto entry{_address_cache.find(mes.sender_ip_address)};
    entry->second.address = mes.sender_ethernet_address;
    entry->second.expire_time = _time_pass + 30000;
    if (!_unkonw_ip_datagarm.count(mes.sender_ip_address) ||
        _unkonw_ip_datagarm.find(mes.sender_ip_address)->second.empty()) {
        return;
    }

    while (!_unkonw_ip_datagarm.find(mes.sender_ip_address)->second.empty()) {
        auto ehter_dst{mes.sender_ethernet_address};
        auto frame{EthernetFrame{}};
        auto &header{frame.header()};
        header.type = EthernetHeader::TYPE_IPv4;
        header.src = _ethernet_address;
        header.dst = ehter_dst;
        frame.payload() = _unkonw_ip_datagarm.find(mes.sender_ip_address)->second.front().serialize();
        _unkonw_ip_datagarm.find(mes.sender_ip_address)->second.pop();
        _frames_out.emplace(frame);
    }
    _unkonw_ip_datagarm.erase(_unkonw_ip_datagarm.find(mes.sender_ip_address));
    return;
}
