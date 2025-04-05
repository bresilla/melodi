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

// Unified IPv6 packet structure with fragmentation support.
typedef struct {
    uint8_t version;                    // Always 6 for IPv6
    uint8_t hopLimit;                   // Hop Limit (TTL)
    uint16_t sequenceNumber;            // Common sequence number for all fragments of a message
    uint8_t fragmentIndex;              // Index of this fragment (0 if unfragmented)
    uint8_t fragmentCount;              // Total number of fragments (1 if unfragmented)
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
