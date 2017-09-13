local ffi = require "ffi"
local pktLib = require "packet"
local eth = require "proto.ethernet"
local ip = require "proto.ip4"

local module = {}

-- Define wanted flow keys and flow state
-- must be done on top level to be available/defined in all threads
ffi.cdef [[
    struct my_flow_state {
        uint64_t packet_counter;
        uint64_t byte_counter;
        uint64_t first_seen;
        uint64_t last_seen;
        uint8_t some_flags;
        uint16_t some_fancy_data[20];
    };

    struct my_primary_flow_key {
        uint32_t ip_dst;
        uint32_t ip_src;
        uint16_t port_dst;
        uint16_t port_src;
        uint8_t  proto;
    } __attribute__((__packed__));

    struct my_secondary_flow_key {
        uint8_t  ip_dst[16];
        uint8_t  ip_src[16];
        uint16_t port_dst;
        uint16_t port_src;
        uint8_t  proto;
    } __attribute__((__packed__));
]]

-- Export names of structs
-- secondaryFlowKey is optional
module.stateType = "struct my_flow_state"
module.primaryFlowKey = "struct my_primary_flow_key"
module.secondaryFlowKey = "struct my_secondary_flow_key"

-- Custom default state for new flows
-- See ffi.new() for table initializer rules
module.defaultState = {packet_counter = 123, some_flags = 0xab}

-- Function that builds the appropiate flow key for the packet given in buf
-- return true for successful extraction, false if a packet should be ignored
-- Use libmoons packet library to access common protocol fields
function module.extractFlowKey(buf, primaryFlowKey, secondaryFlowKey)
    local ethPkt = pktLib.getEthernetPacket(buf)
    if ethPkt.eth:getType() == eth.TYPE_IP then
        -- actual L4 type doesn't matter
        local parsedPkt = pktLib.getUdp4Packet(buf)
        primaryFlowKey.ip_dst = parsedPkt.ip4:getDst()
        primaryFlowKey.ip_src = parsedPkt.ip4:getSrc()
        local TTL = parsedPkt.ip4:getTTL()
        if parsedPkt.ip4:getProtocol() == ip.PROTO_UDP then
            primaryFlowKey.port_dst = parsedPkt.udp:getDstPort()
            primaryFlowKey.port_src = parsedPkt.udp:getSrcPort()
            primaryFlowKey.proto = parsedPkt.ip4:getProtocol()
            return true
        elseif parsedPkt.ip4:getProtocol() == ip.PROTO_TCP then
            -- port at the same position as UDP
            primaryFlowKey.port_dst = parsedPkt.udp:getDstPort()
            primaryFlowKey.port_src = parsedPkt.udp:getSrcPort()
            primaryFlowKey.proto = parsedPkt.ip4:getProtocol()
            return true
        elseif parsedPkt.ip4:getProtocol() == ip.PROTO_SCTP then
            -- port at the same position as UDP
            primaryFlowKey.port_dst = parsedPkt.udp:getDstPort()
            primaryFlowKey.port_src = parsedPkt.udp:getSrcPort()
            primaryFlowKey.proto = parsedPkt.ip4:getProtocol()
            return true
        end
    else
        log:info("Packet not IP")
    end
    return false
end

-- state starts out empty if it doesn't exist yet; buf is whatever the device queue or QQ gives us
-- flowKey will be a ctype of one of the above defined flow keys
function module.handlePacket(flowKey, state, buf, isFirstPacket)
    -- implicit lock by TBB
    state.packet_counter = state.packet_counter + 1
    state.byte_counter = state.byte_counter + buf:getSize()
    if isFirstPacket then
        state.first_seen = time()
    end
    state.last_seen = time()
    -- can add custom "active timeout" (like ipfix) here
end

-- Function that gets called in regular intervals to decide if a flow is still active
-- Returns true for flows that are expired, false for active flows
function module.checkExpiry(flowKey, state)
    if math.random(0, 200) == 0 then
        return true
    else
        return false
    end
end

-- Set the interval in which the check function should be called
-- float in seconds
module.checkInterval = 5

return module
