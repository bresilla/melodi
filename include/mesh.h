#ifndef MESH_H
#define MESH_H

#include <Arduino.h>
#include <RH_RF95.h>
#include <stdint.h>
#include <string.h>

#define IPV6_ADDR_LEN 16
#define MAX_PAYLOAD_SIZE 20 // Maximum bytes per fragment
#define MAX_FRAGMENTS 10
#define MAX_MESSAGE_SIZE (MAX_FRAGMENTS * MAX_PAYLOAD_SIZE)

// Define a broadcast IPv6 address (all bytes set to 0xFF)
extern const uint8_t BROADCAST_ADDRESS[IPV6_ADDR_LEN];

// Pack packetID, fragmentIndex, and fragmentCount into 16 bits.
typedef struct {
    uint16_t packetID : 4;      // 0-15
    uint16_t fragmentIndex : 4; // 0-15 (max 16 fragments)
    uint16_t fragmentCount : 4; // 0-15 (max 16 fragments)
    uint16_t reserved : 4;      // Reserved for future use
} FragInfo;

// Unified IPv6 packet structure with fragmentation support.
// Removed version field since it's constant (always 6).
typedef struct {
    uint8_t hopLimit;                   // Hop Limit (TTL)
    uint16_t sequenceNumber;            // Global sequence number
    FragInfo fragInfo;                  // Packed field for packetID, fragmentIndex, and fragmentCount
    uint8_t payloadLength;              // Number of valid bytes in the payload
    uint8_t source[IPV6_ADDR_LEN];      // Source IPv6 address
    uint8_t destination[IPV6_ADDR_LEN]; // Destination IPv6 address
    char payload[MAX_PAYLOAD_SIZE];     // Message payload fragment
} IPv6Packet;

// Mesh functions:
void setRadio(RH_RF95 *radio);
void initIPv6Address(uint8_t nodeId, uint8_t *addr);
bool ipv6Equal(const uint8_t *addr1, const uint8_t *addr2);
void ipv6ToString(const uint8_t *addr, char *buffer, size_t bufferLen);
void sendIPv6Message(const uint8_t *srcAddr, const uint8_t *destAddr, const char *message);
void forwardPacket(IPv6Packet *packet);
bool reassembleFragment(const IPv6Packet *packet, char *outMessage, size_t outMessageSize);

#endif
