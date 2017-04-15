#include <cstdint>
#include <vector>

#include <scoped_allocator>
#include <rte_allocator.hpp>

#include <rte_common.h>
#include <rte_config.h>
#include <rte_lcore.h>
#include <rte_hash.h>
#include <rte_jhash.h>
#include <rte_debug.h>
#include <rte_errno.h>
#include <rte_malloc.h>

template<typename T> using alloc = std::scoped_allocator_adaptor<rte_allocator<T>>;

namespace flowtracker {
    constexpr uint8_t IPV6_ADDR_LEN = 16;
    
    struct ipv4_5tuple {
        uint32_t ip_dst;
        uint32_t ip_src;
        uint16_t port_dst;
        uint16_t port_src;
        uint8_t  proto;
    } __attribute__((__packed__));
    
    struct ipv6_5tuple {
        uint8_t  ip_dst[IPV6_ADDR_LEN];
        uint8_t  ip_src[IPV6_ADDR_LEN];
        uint16_t port_dst;
        uint16_t port_src;
        uint8_t  proto;
    } __attribute__((__packed__));
    
    template<typename T>
    struct flowtracker {
        explicit flowtracker(const std::uint32_t max_flows = 1024) :
                ipv4_flowdata(std::vector<T, alloc<T>>(max_flows)),
                ipv6_flowdata(std::vector<T>(max_flows)) {            
            rte_hash_parameters params = {};
            params.entries = max_flows;
            params.key_len = sizeof(ipv4_5tuple);
            params.hash_func = rte_jhash;
            params.hash_func_init_val = 0;
            params.socket_id = rte_socket_id();
            params.name = "ipv4_flow_map";
            ipv4_map = rte_hash_create(&params);
            if(ipv4_map == NULL)
                rte_panic("Could not create IPv4 flow hash map, errno = %d\n", rte_errno);
            
            params.key_len = sizeof(ipv6_5tuple);
            params.name = "ipv6_flow_map";
            ipv6_map = rte_hash_create(&params);
            if(ipv6_map == NULL)
                rte_panic("Could not create IPv6 flow hash map, errno = %d\n", rte_errno);
        }
        
        ~flowtracker() {
            rte_hash_free(const_cast<rte_hash*>(ipv4_map));
            rte_hash_free(const_cast<rte_hash*>(ipv6_map));
        }
        
        template<typename F>
        std::int32_t add_flow(const F& flow_tuple, const T* flow_data);
        
        std::int32_t add_flow(const ipv4_5tuple& flow_tuple, const T& flow_data) {
            std::int32_t ret = rte_hash_add_key(this->ipv4_map, flow_tuple);
            if (ret < 0)
                return ret;
            this->ipv4_flowdata[ret] = flow_data;
        }
        
        std::int32_t add_flow(const ipv6_5tuple& flow_tuple, const T& flow_data) {
            std::int32_t ret = rte_hash_add_key(this->ipv6_map, flow_tuple);
            if (ret < 0)
                return ret;
            this->ipv6_flowdata[ret] = flow_data;
        }
        
        template<typename F>
        T* get_flow_data(const F& flow_tuple);
        
        T* get_flow_data(const ipv4_5tuple& flow_tuple) {
            std::int32_t ret = rte_hash_lookup(this->ipv4_map, &flow_tuple);
            if (ret < 0)
                return NULL;
            return &this->ipv4_flowdata[ret];
        }
        
        T* get_flow_data(const ipv6_5tuple& flow_tuple) {
            std::int32_t ret = rte_hash_lookup(this->ipv6_flow_map, flow_tuple);
            if (ret < 0)
                return NULL;
            return this->ipv6_flowdata[ret];
        }
        
        template<typename F>
        std::int32_t remove_flow(const F& flow_tuple);
        
        std::int32_t remove_flow(const ipv4_5tuple& flow_tuple) {
            return rte_hash_del_key(this->ipv4_flow_map, flow_tuple);
        }
        
        std::int32_t remove_flow(const ipv6_5tuple& flow_tuple) {
            return rte_hash_del_key(this->ipv6_flow_map, flow_tuple);
        }
        
        
    private:
        const struct rte_hash* ipv4_map;
        const struct rte_hash* ipv6_map;
        std::vector<T, alloc<T>> ipv4_flowdata;
        std::vector<T> ipv6_flowdata;
    };
}



extern "C" {
    // Copied from QQ.hpp
    struct packet_header {  
        uint64_t timestamp:48;  //!< Stores a timestamp. Unit is microseconds.
        uint64_t vlan:12;       //!< Field to store the VLAN tag. Prevents messy Ethernet header.
        uint16_t len;           //!< Holds the length of the data array.
        uint8_t data[];         //!< Flexible array member. Valid since C99, not really in C++.
    } __attribute__((__packed__));
    
    struct foo_flow_data {
        uint64_t start_ts;
        uint64_t end_ts;
        uint8_t observed_ttl;
    } __attribute__((__packed__));
    
    using D = foo_flow_data;
    using tracker = flowtracker::flowtracker<D>;
    using v4tpl = flowtracker::ipv4_5tuple;
    using v6tpl = flowtracker::ipv6_5tuple;
    
    tracker* flowtracker_create(uint32_t max_flows) {
        void* temp = rte_malloc(NULL, sizeof(tracker), 0);
        return new(temp) tracker(max_flows);
    }
    
    void flowtracker_delete(tracker* tr) {
        delete tr;
    }
    
    /*
    int32_t flowtracker_add_flow_v4(tracker* tr, const v4tpl* const tpl, const D* flow_data) {
        return tr->add_flow(tpl, flow_data);
    }
    */
    
    int32_t flowtracker_add_flow_v4(tracker* tr, uint32_t ip_src, uint16_t port_src,
                                    uint32_t ip_dst, uint16_t port_dst, uint8_t proto,
                                    const D* flow_data) {
        flowtracker::ipv4_5tuple tpl {
            ip_dst, ip_src, port_dst, port_src, proto
        };
        return tr->add_flow(tpl, flow_data);
    }
    
    int32_t flowtracker_add_flow_v6(tracker* tr, const v6tpl* const tpl, const D* flow_data) {
        return tr->add_flow(tpl, flow_data);
    }
    
    int32_t flowtracker_remove_flow_v4(tracker* tr, const flowtracker::ipv4_5tuple* const tpl) {
        return tr->remove_flow(tpl);
    }
    
    int32_t flowtracker_remove_flow_v6(tracker* tr, const flowtracker::ipv6_5tuple* const tpl) {
        return tr->remove_flow(tpl);
    }
    
    /*
    foo_flow_data* flowtracker_get_flow_data_v4(tracker* tr, const flowtracker::ipv4_5tuple* const tpl) {
        return tr->get_flow_data(tpl);
    }
    */
    
    foo_flow_data* flowtracker_get_flow_data_v6(tracker* tr, const flowtracker::ipv6_5tuple* const tpl) {
        return tr->get_flow_data(tpl);
    }
    
    foo_flow_data* flowtracker_get_flow_data_v4(tracker* tr, uint32_t ip_src, uint16_t port_src,
                                                uint32_t ip_dst, uint16_t port_dst, uint8_t proto) {
        flowtracker::ipv4_5tuple tpl {
            ip_dst, ip_src, port_dst, port_src, proto
        };
        return tr->get_flow_data(tpl);
    }
    
    void analyze(tracker* tr, const packet_header* const pkt_hdr);
    
    void analyze_v4(tracker* tr, uint64_t ts, const flowtracker::ipv4_5tuple* const tpl, const uint8_t ttl);
    
    void analyze_v6(tracker* tr, uint64_t ts, const flowtracker::ipv6_5tuple* const tpl, const uint8_t ttl);
    
}
