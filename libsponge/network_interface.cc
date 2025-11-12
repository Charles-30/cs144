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
void DUMMY_CODE(Targs &&... /* unused */) {}

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

    // 检查ARP缓存表中是否有对应的以太网地址
    auto arp_entry = _arp_cache.find(next_hop_ip);
    if (arp_entry != _arp_cache.end()) {
        // 已知以太网地址，创建以太网帧并发送
        EthernetFrame frame;
        frame.header().type = EthernetHeader::TYPE_IPv4;
        frame.header().src = _ethernet_address;
        frame.header().dst = arp_entry->second.mac;
        frame.payload() = dgram.serialize();
        _frames_out.push(frame);
    } else {
        // 未知以太网地址，排列数据报，发送ARP请求
        bool need_arp_request = true;

        // 检查是否已有未决的ARP请求
        auto pending_arp = _pending_arps.find(next_hop_ip);
        if (pending_arp != _pending_arps.end()) {
            // 已有未决请求，添加数据报到队列
            pending_arp->second.datagrams.push_back(dgram);
            need_arp_request = false;
        }

        if(need_arp_request) {
            // 创建新的未决ARP请求
            PendingARP new_pending;
            new_pending.timestamp = _current_time;
            new_pending.datagrams.push_back(dgram);
            _pending_arps[next_hop_ip] = new_pending;

            // 发送 ARP请求
            ARPMessage arp_request;
            arp_request.opcode = ARPMessage::OPCODE_REQUEST;
            arp_request.sender_ethernet_address = _ethernet_address;
            arp_request.sender_ip_address = _ip_address.ipv4_numeric();
            arp_request.target_ethernet_address = {0, 0, 0, 0, 0, 0};// unknown
            arp_request.target_ip_address = next_hop_ip;

            EthernetFrame arp_frame;
            arp_frame.header().type = EthernetHeader::TYPE_ARP;
            arp_frame.header().src = _ethernet_address;
            arp_frame.header().dst = ETHERNET_BROADCAST; // broadcast
            arp_frame.payload() = arp_request.serialize();
            _frames_out.push(arp_frame);
        }
    }
}


//! \param[in] frame the incoming Ethernet frame
optional<InternetDatagram> NetworkInterface::recv_frame(const EthernetFrame &frame) {
    // 检查目标MAC地址是否为本接口地址或广播地址
    if (frame.header().dst != _ethernet_address && frame.header().dst != ETHERNET_BROADCAST) {
        return nullopt; // 丢弃帧
    }
    
    if (frame.header().type == EthernetHeader::TYPE_IPv4) {
        // IPv4:解析并返回数据报
        InternetDatagram dgram;
        if (dgram.parse(frame.payload()) == ParseResult::NoError) {
            return dgram;
        }
    } else if (frame.header().type == EthernetHeader::TYPE_ARP) {
        // ARP:解析并处理ARP消息
        ARPMessage arp_msg;
        if (arp_msg.parse(frame.payload()) == ParseResult::NoError) {
            // 记住发送者的IP和以太网地址映射关系
            _arp_cache[arp_msg.sender_ip_address] = ARPEntry(arp_msg.sender_ethernet_address, ARP_ENTRY_TTL);

            // 处理ARP请求
            if (arp_msg.opcode == ARPMessage::OPCODE_REQUEST && arp_msg.target_ip_address == _ip_address.ipv4_numeric()) {
                // 这个ARP请求是发给本接口的，发送ARP回复
                ARPMessage arp_reply;
                arp_reply.opcode = ARPMessage::OPCODE_REPLY;
                arp_reply.sender_ethernet_address = _ethernet_address;
                arp_reply.sender_ip_address = _ip_address.ipv4_numeric();
                arp_reply.target_ethernet_address = arp_msg.sender_ethernet_address;
                arp_reply.target_ip_address = arp_msg.sender_ip_address;

                EthernetFrame reply_frame;
                reply_frame.header().type = EthernetHeader::TYPE_ARP;
                reply_frame.header().src = _ethernet_address;
                reply_frame.header().dst = arp_msg.sender_ethernet_address;
                reply_frame.payload() = arp_reply.serialize();
                _frames_out.push(reply_frame);
            }

            // 处理ARP回复
            if (arp_msg.opcode == ARPMessage::OPCODE_REPLY) {
                auto pending_arp = _pending_arps.find(arp_msg.sender_ip_address);
                if (pending_arp != _pending_arps.end()) {
                    // 发送之前排队的数据报
                    for (const auto &queued_dgram : pending_arp->second.datagrams) {
                        EthernetFrame dgram_frame;
                        dgram_frame.header().type = EthernetHeader::TYPE_IPv4;
                        dgram_frame.header().src = _ethernet_address;
                        dgram_frame.header().dst = arp_msg.sender_ethernet_address;
                        dgram_frame.payload() = queued_dgram.serialize();
                        _frames_out.push(dgram_frame);
                    }
                    // 移除未决ARP请求
                    _pending_arps.erase(pending_arp);
                }
            }
        }
    }

    return nullopt;
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick(const size_t ms_since_last_tick) {
    _current_time += ms_since_last_tick;

    // 更新ARP缓存表
    for (auto it = _arp_cache.begin(); it != _arp_cache.end(); ) {
        if (it->second.ttl <= ms_since_last_tick) {
            it = _arp_cache.erase(it); //移除过期表项
        } else {
            it->second.ttl -= ms_since_last_tick;
            ++it;
        }
    }

    // 更新未决ARP请求
    for (auto it = _pending_arps.begin(); it != _pending_arps.end(); ) {
        if (_current_time - it->second.timestamp >= ARP_REQUEST_TTL) {
            // 重新发送ARP请求
            ARPMessage arp_request;
            arp_request.opcode = ARPMessage::OPCODE_REQUEST;
            arp_request.sender_ethernet_address = _ethernet_address;
            arp_request.sender_ip_address = _ip_address.ipv4_numeric();
            arp_request.target_ethernet_address = {0, 0, 0, 0, 0, 0};
            arp_request.target_ip_address = it->first;

            EthernetFrame arp_frame;
            arp_frame.header().type = EthernetHeader::TYPE_ARP;
            arp_frame.header().src = _ethernet_address;
            arp_frame.header().dst = ETHERNET_BROADCAST; // broadcast
            arp_frame.payload() = arp_request.serialize();
            _frames_out.push(arp_frame);

            // 更新时间戳
            it->second.timestamp = _current_time;
            ++it;
        } else {
            ++it;
        }
    }
}