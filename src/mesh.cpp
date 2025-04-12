#include "mesh.h"

// Define the broadcast address as all 0xFF.
const uint8_t BROADCAST_ADDRESS[IPV6_ADDR_LEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// Global counters for packet sequencing.
static uint16_t globalSequenceNumber = 0; // Will be used for the 4-bit sequenceNumber (lower 4 bits).
static uint8_t globalPacketID = 0;        // Cycles 0-15.

// Pointer to the radio instance used by the mesh functions.
static RH_RF95 *meshRadio = nullptr;

void setRadio(RH_RF95 *radio) { meshRadio = radio; }

void initIPv6Address(uint8_t nodeId, uint8_t *addr) {
    memset(addr, 0, IPV6_ADDR_LEN);
    addr[IPV6_ADDR_LEN - 1] = nodeId;
}

bool ipv6Equal(const uint8_t *addr1, const uint8_t *addr2) { return (memcmp(addr1, addr2, IPV6_ADDR_LEN) == 0); }

void ipv6ToString(const uint8_t *addr, char *buffer, size_t bufferLen) {
    int pos = 0;
    for (int i = 0; i < IPV6_ADDR_LEN; i++) {
        if (i > 0) {
            pos += snprintf(buffer + pos, bufferLen - pos, ":");
        }
        pos += snprintf(buffer + pos, bufferLen - pos, "%02X", addr[i]);
    }
}

// sendIPv6Message that sends each fragment repeatCount times.
void sendIPv6Message(const uint8_t *srcAddr, const uint8_t *destAddr, const char *message, uint8_t repeatCount) {
    if (!meshRadio) {
        return;
    }
    int msgLen = strlen(message);
    // Calculate number of fragments required.
    int fragCount = (msgLen + MAX_PAYLOAD_SIZE - 1) / MAX_PAYLOAD_SIZE;
    if (fragCount > MAX_FRAGMENTS) {
        fragCount = MAX_FRAGMENTS; // Optionally log that message was truncated.
    }
    // Get a global sequence (only the lower 4 bits are used) and a packetID.
    uint16_t seq = globalSequenceNumber++;
    uint8_t currentPacketID = globalPacketID;
    globalPacketID = (globalPacketID + 1) % 16;

    // Loop over each fragment.
    for (int frag = 0; frag < fragCount; frag++) {
        IPv6Packet packet;
        packet.hopLimit = 10;
        // Fill in the FragInfo structure:
        packet.fragInfo.packetID = currentPacketID; // Packet identifier.
        packet.fragInfo.fragmentIndex = frag;       // Fragment index.
        packet.fragInfo.fragmentCount = fragCount;  // Total number of fragments.
        // Use the lower 4 bits of the global sequence number for per-message sequencing.
        packet.fragInfo.sequenceNumber = seq & 0x0F;

        int start = frag * MAX_PAYLOAD_SIZE;
        int remaining = msgLen - start;
        int fragLen = (remaining > MAX_PAYLOAD_SIZE) ? MAX_PAYLOAD_SIZE : remaining;
        packet.payloadLength = fragLen;

        memcpy(packet.source, srcAddr, IPV6_ADDR_LEN);
        memcpy(packet.destination, destAddr, IPV6_ADDR_LEN);
        memcpy(packet.payload, message + start, fragLen);
        // Zero-pad if necessary.
        if (fragLen < MAX_PAYLOAD_SIZE) {
            memset(packet.payload + fragLen, 0, MAX_PAYLOAD_SIZE - fragLen);
        }

        // Transmit each fragment repeatCount times.
        for (uint8_t r = 0; r < repeatCount; r++) {
            meshRadio->send((uint8_t *)&packet, sizeof(IPv6Packet));
            meshRadio->waitPacketSent();
        }
    }
}

void forwardPacket(IPv6Packet *packet) {
    if (!meshRadio) {
        return;
    }
    if (packet->hopLimit > 0) {
        packet->hopLimit--;
        meshRadio->send((uint8_t *)packet, sizeof(IPv6Packet));
        meshRadio->waitPacketSent();
    }
}

// --- Reassembly Mechanism ---
#define MAX_REASSEMBLY_CONTEXTS 5
#define REASSEMBLY_TIMEOUT 30000UL // 30 seconds

typedef struct {
    bool inUse;
    uint8_t source[IPV6_ADDR_LEN];
    uint8_t packetID;       // From FragInfo.packetID
    uint8_t sequenceNumber; // New: from FragInfo.sequenceNumber
    uint8_t expectedFragments;
    bool received[MAX_FRAGMENTS];
    uint8_t fragmentLengths[MAX_FRAGMENTS];
    char buffer[MAX_MESSAGE_SIZE];
    unsigned long lastUpdate;
} ReassemblyContext;

static ReassemblyContext reassemblyContexts[MAX_REASSEMBLY_CONTEXTS];

static ReassemblyContext *getReassemblyContext(const IPv6Packet *packet) {
    unsigned long now = millis();
    // Clean up timed-out contexts.
    for (int i = 0; i < MAX_REASSEMBLY_CONTEXTS; i++) {
        if (reassemblyContexts[i].inUse && (now - reassemblyContexts[i].lastUpdate > REASSEMBLY_TIMEOUT)) {
            reassemblyContexts[i].inUse = false;
        }
    }
    // Look for an existing context matching the sender, packetID, and sequenceNumber.
    for (int i = 0; i < MAX_REASSEMBLY_CONTEXTS; i++) {
        if (reassemblyContexts[i].inUse && (reassemblyContexts[i].packetID == packet->fragInfo.packetID) &&
            (reassemblyContexts[i].sequenceNumber == packet->fragInfo.sequenceNumber) && ipv6Equal(reassemblyContexts[i].source, packet->source)) {
            return &reassemblyContexts[i];
        }
    }
    // Allocate a new context if available.
    for (int i = 0; i < MAX_REASSEMBLY_CONTEXTS; i++) {
        if (!reassemblyContexts[i].inUse) {
            reassemblyContexts[i].inUse = true;
            memcpy(reassemblyContexts[i].source, packet->source, IPV6_ADDR_LEN);
            reassemblyContexts[i].packetID = packet->fragInfo.packetID;
            reassemblyContexts[i].sequenceNumber = packet->fragInfo.sequenceNumber;
            reassemblyContexts[i].expectedFragments = packet->fragInfo.fragmentCount;
            for (int j = 0; j < MAX_FRAGMENTS; j++) {
                reassemblyContexts[i].received[j] = false;
                reassemblyContexts[i].fragmentLengths[j] = 0;
            }
            memset(reassemblyContexts[i].buffer, 0, MAX_MESSAGE_SIZE);
            reassemblyContexts[i].lastUpdate = now;
            return &reassemblyContexts[i];
        }
    }
    return NULL; // No context available.
}

bool reassembleFragment(const IPv6Packet *packet, char *outMessage, size_t outMessageSize) {
    ReassemblyContext *ctx = getReassemblyContext(packet);
    if (ctx == NULL) {
        // No available context.
        return false;
    }
    // Update the last update time.
    ctx->lastUpdate = millis();
    if (packet->fragInfo.fragmentIndex >= MAX_FRAGMENTS) {
        return false;
    }
    if (ctx->received[packet->fragInfo.fragmentIndex]) {
        return false; // Duplicate fragment.
    }
    int offset = packet->fragInfo.fragmentIndex * MAX_PAYLOAD_SIZE;
    if (offset + packet->payloadLength > MAX_MESSAGE_SIZE) {
        return false;
    }
    memcpy(ctx->buffer + offset, packet->payload, packet->payloadLength);
    ctx->fragmentLengths[packet->fragInfo.fragmentIndex] = packet->payloadLength;
    ctx->received[packet->fragInfo.fragmentIndex] = true;

    // Check if all expected fragments have been received.
    bool complete = true;
    for (int i = 0; i < ctx->expectedFragments; i++) {
        if (!ctx->received[i]) {
            complete = false;
            break;
        }
    }
    if (complete) {
        int totalLength = 0;
        for (int i = 0; i < ctx->expectedFragments; i++) {
            totalLength += ctx->fragmentLengths[i];
        }
        if ((size_t)totalLength >= outMessageSize) {
            return false;
        }
        memcpy(outMessage, ctx->buffer, totalLength);
        outMessage[totalLength] = '\0';
        ctx->inUse = false; // Clear context after successful reassembly.
        return true;
    }
    return false;
}
