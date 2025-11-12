#ifndef SPONGE_LIBSPONGE_NETWORK_INTERFACE_HH
#define SPONGE_LIBSPONGE_NETWORK_INTERFACE_HH

#include "ethernet_frame.hh"
#include "tcp_over_ip.hh"
#include "tun.hh"

#include <optional>
#include <queue>
#include <unordered_map>
#include <list>

//! \brief A "network interface" that connects IP (the internet layer, or network layer)
//! with Ethernet (the network access layer, or link layer).

class NetworkInterface {
  private:
    //! Ethernet (known as hardware, network-access-layer, or link-layer) address of the interface
    EthernetAddress _ethernet_address;

    //! IP (known as internet-layer or network-layer) address of the interface
    Address _ip_address;

    //! outbound queue of Ethernet frames that the NetworkInterface wants sent
    std::queue<EthernetFrame> _frames_out{};

    // ARP cache: maps IP address to {MAC address, TTL}
    struct ARPEntry {
        EthernetAddress mac{};
        size_t ttl{0};
        
        ARPEntry() = default;
        ARPEntry(const EthernetAddress &mac_addr, size_t time_to_live) 
            : mac(mac_addr), ttl(time_to_live) {}
    };
    std::unordered_map<uint32_t, ARPEntry> _arp_cache{};

    // Pending ARP requests: maps IP address to {timestamp, queued datagrams}
    struct PendingARP {
        size_t timestamp{0};
        std::list<InternetDatagram> datagrams{};
        
        PendingARP() = default;
        PendingARP(size_t ts, const InternetDatagram &first_dgram) 
            : timestamp(ts) {
            datagrams.push_back(first_dgram);
        }
    };
    std::unordered_map<uint32_t, PendingARP> _pending_arps{};

    // Current time in milliseconds
    size_t _current_time{0};

    // Constants
    static constexpr size_t ARP_ENTRY_TTL = 30000; // 30 seconds for ARP cache entries
    static constexpr size_t ARP_REQUEST_TTL = 5000; // 5 seconds for ARP requests

  public:
    //! \brief Construct a network interface with given Ethernet (network-access-layer) and IP (internet-layer) addresses
    NetworkInterface(const EthernetAddress &ethernet_address, const Address &ip_address);

    //! \brief Access queue of Ethernet frames awaiting transmission
    std::queue<EthernetFrame> &frames_out() { return _frames_out; }

    //! \brief Sends an IPv4 datagram, encapsulated in an Ethernet frame (if it knows the Ethernet destination address).
    void send_datagram(const InternetDatagram &dgram, const Address &next_hop);

    //! \brief Receives an Ethernet frame and responds appropriately.
    std::optional<InternetDatagram> recv_frame(const EthernetFrame &frame);

    //! \brief Called periodically when time elapses
    void tick(const size_t ms_since_last_tick);
};

#endif  // SPONGE_LIBSPONGE_NETWORK_INTERFACE_HH