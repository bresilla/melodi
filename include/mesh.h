#ifndef MESH_H
#define MESH_H

#include <Arduino.h>
#include <RH_RF95.h>
#include <stdint.h>
#include <string.h>

#define IPV6_ADDR_LEN 16
// LORA MAX MESSAGE SIZE 255 Bytes
// 16 * 255 bits = 4096 bits (without headers)
#define MAX_PAYLOAD_SIZE (16 * 8) // Maximum bits per fragment
#define MAX_FRAGMENTS 16
#define MAX_MESSAGE_SIZE (MAX_FRAGMENTS * MAX_PAYLOAD_SIZE)

// Define a broadcast IPv6 address (all bytes set to 0xFF)
extern const uint8_t BROADCAST_ADDRESS[IPV6_ADDR_LEN];
extern const uint8_t IGNORE_ADDRESS[IPV6_ADDR_LEN];

#define MAX_REASSEMBLY_CONTEXTS 10
#define REASSEMBLY_TIMEOUT 30000UL // 30 seconds

// Duplicate detection
#define MAX_RECENT_MESSAGES 20
#define DUPLICATE_TIMEOUT 5000UL // 5 seconds

typedef struct {
    bool active;
    bool broadcast;
    uint8_t source[IPV6_ADDR_LEN];
    uint8_t packetID;
    uint8_t fragmentTotal;
    uint8_t lastFragmentLength;
    bool fragmentsReceived[MAX_FRAGMENTS];
    uint8_t dataBuffer[MAX_MESSAGE_SIZE];
    unsigned long lastUpdate;
} ReassemblyContext;

static ReassemblyContext contexts[MAX_REASSEMBLY_CONTEXTS];

typedef struct {
    bool broadcast;
    uint8_t source[IPV6_ADDR_LEN];
    uint16_t payloadLength;
    uint8_t payload[MAX_MESSAGE_SIZE];
} ReassembledPacket;

// Structure to track recent messages for duplicate detection
typedef struct {
    bool active;
    uint32_t hash;
    uint8_t source[IPV6_ADDR_LEN];
    unsigned long timestamp;
} RecentMessage;

// Fragment info
typedef struct __attribute__((packed)) {
    uint16_t packetID : 4;      // Packet ID (4 bits)
    uint16_t hopLimit : 4;      // Hop limit TTL (4 bits)
    uint16_t fragmentIndex : 4; // Fragment index (4 bits)
    uint16_t fragmentTotal : 4; // Total fragments (4 bits)
} FragInfo;                     // 16 bits

// Unified IPv6 packet structure with fragmentation support.
typedef struct __attribute__((packed)) {
    FragInfo fragInfo;                  // Fragment info  (16 bits)
    uint8_t payloadLength;              // Number of valid bytes (8 bits)
    uint8_t source[IPV6_ADDR_LEN];      // Source IPv6 address (128 bits)
    uint8_t destination[IPV6_ADDR_LEN]; // Destination IPv6 address (128 bits)
    uint8_t payload[MAX_PAYLOAD_SIZE];  // Message payload fragment (1024 bits)
} IPv6Packet;                           // 1472 bits

// Mesh functions:
void setRadio(RH_RF95 *radio);
void toIPv6Address(uint64_t ipv6_first, uint64_t ipv6_last, uint8_t *nodeAddress);
bool ipv6Equal(const uint8_t *addr1, const uint8_t *addr2);
void ipv6ToString(const uint8_t *addr, char *buffer, size_t bufferLen);
void sendIPv6Message(const uint8_t *srcAddr, const uint8_t *destAddr, const uint8_t *message, size_t msgLen, uint8_t repeatCount);
void forwardPacket(IPv6Packet *packet);
void safePrint(const char *format, ...);
void safePrintln(const char *format, ...);
void serialSendReassembledPacket(ReassembledPacket *reassembledPacket);

void createOrUpdateContext(const IPv6Packet *packet);
void deleteOldContexts();
bool getCompletedContext(ReassembledPacket *reassembledPacket);

// Duplicate detection functions
uint32_t calculateMessageHash(const uint8_t *data, size_t length, const uint8_t *source);
bool isDuplicateMessage(uint32_t hash, const uint8_t *source);
void addRecentMessage(uint32_t hash, const uint8_t *source);
void cleanupOldMessages();

#endif
