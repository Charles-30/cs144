#include "router.hh"

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
void DUMMY_CODE(Targs &&... /* unused */) {}

//! \param[in] route_prefix The "up-to-32-bit" IPv4 address prefix to match the datagram's destination address against
//! \param[in] prefix_length For this route to be applicable, how many high-order (most-significant) bits of the route_prefix will need to match the corresponding bits of the datagram's destination address?
//! \param[in] next_hop The IP address of the next hop. Will be empty if the network is directly attached to the router (in which case, the next hop address should be the datagram's final destination).
//! \param[in] interface_num The index of the interface to send the datagram out on.
// 此方法将路由添加到路由表中，你需要在Router类中添加一个路由表作为私有成员来存储这些信息，保存路由以备以后使用，即需要修改.hh文件。之后，调用此函数，将表项存入路由表。
void Router::add_route(const uint32_t route_prefix,
                       const uint8_t prefix_length,
                       const optional<Address> next_hop,
                       const size_t interface_num) {
    cerr << "DEBUG: adding route " << Address::from_ipv4_numeric(route_prefix).ip() << "/" << int(prefix_length)
         << " => " << (next_hop.has_value() ? next_hop->ip() : "(direct)") << " on interface " << interface_num << "\n";

    // 将路由表项添加到路由表中
    _route_table.emplace_back(route_prefix, prefix_length, next_hop, interface_num);
}

//! \param[in] dgram The datagram to be routed
// 此方法需要将一个数据报从适当的接口路由到下一跳，它需要实现IP路由器的“最长前缀匹配”逻辑，以找到最好的路由。
// 首先检查数据报的ttl是否满足，若不满足，直接丢弃。如果满足，ttl-1，并重新计算校验和（check_sum）。
// 路由器在路由表中查找数据报目的地址匹配的路由，即目的地址的最长prefix_length与route_prefix的最长prefix_length相同。在匹配的路由中，路由器选择prefix_length最长的路由。如果没有匹配的路由，则丢弃该数据报。否则，路由器将数据报从接口发送到适当的下一跳。
void Router::route_one_datagram(InternetDatagram &dgram) {
    // 检查TTL
    if (dgram.header().ttl <= 1) {
        return; // TTL不足，丢弃数据报
    }

    // 减少TTL
    dgram.header().ttl--;

    // 目标IP地址
    const uint32_t dst_ip = dgram.header().dst;

    // 寻找最长前缀匹配
    const RouteEntry *best_route = nullptr;
    size_t best_prefix_length = 0;

    for (const auto &route : _route_table) {
        // 跳过无效接口
        if (route.interface_num >= _interfaces.size()) {
            continue;
        }

        // 计算掩码
        uint32_t mask = 0;
        if (route.prefix_length == 0) {
            mask = 0; // 默认路由匹配所有
        } else if (route.prefix_length == 32) {
            mask = 0xFFFFFFFF; // 精确匹配
        } else {
            mask = (0xFFFFFFFF << (32 - route.prefix_length));
        }
        
        // 检查前缀是否匹配
        if ((dst_ip & mask) == (route.route_prefix & mask)) {
            // 找到匹配的路由，检查是否是更长的前缀
            if (best_route == nullptr || route.prefix_length > best_prefix_length) {
                best_route = &route;
                best_prefix_length = route.prefix_length;
            }
        }
    }

    // 如果没有找到匹配的路由，丢弃数据报
    if (best_route == nullptr) {
        return;
    }

    // 确定下一跳地址 - 立即初始化，避免使用默认构造函数
    Address next_hop_addr = best_route->next_hop.has_value() 
                          ? best_route->next_hop.value() 
                          : Address::from_ipv4_numeric(dst_ip);
    
    // 通过相应的接口发送数据报
    _interfaces[best_route->interface_num].send_datagram(dgram, next_hop_addr);
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
