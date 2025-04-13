#include "mesh.h"

// Define the broadcast address as all 0xFF.
const uint8_t BROADCAST_ADDRESS[IPV6_ADDR_LEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// Define the ignore address as all 0x00.
const uint8_t IGNORE_ADDRESS[IPV6_ADDR_LEN] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

// Global counters for packet sequencing.
static uint16_t globalSequenceNumber = 0; // Will be used for the 4-bit sequenceNumber (lower 4 bits).
static uint8_t globalPacketID = 0;        // Cycles 0-15.

// Pointer to the radio instance used by the mesh functions.
static RH_RF95 *meshRadio = nullptr;

void setRadio(RH_RF95 *radio) { meshRadio = radio; }

void toIPv6Address(uint8_t nodeId, uint8_t *addr) {
    memset(addr, 0, IPV6_ADDR_LEN);
    addr[IPV6_ADDR_LEN - 1] = nodeId;
}

void toIPv6Address(uint64_t ipv6_first, uint64_t ipv6_last, uint8_t *nodeAddress) {
    // Extract 8 bytes from ipv6_first (most significant to least significant)
    for (int i = 0; i < 8; i++) {
        nodeAddress[i] = (ipv6_first >> ((7 - i) * 8)) & 0xFF;
    }
    // Extract 8 bytes from ipv6_last
    for (int i = 0; i < 8; i++) {
        nodeAddress[i + 8] = (ipv6_last >> ((7 - i) * 8)) & 0xFF;
    }
}

bool ipv6Equal(const uint8_t *addr1, const uint8_t *addr2) { return (memcmp(addr1, addr2, IPV6_ADDR_LEN) == 0); }

void ipv6ToString(const uint8_t *addr, char *buffer, size_t bufferLen) {
    int pos = 0;
    for (int group = 0; group < 8; group++) {
        if (group > 0) {
            pos += snprintf(buffer + pos, bufferLen - pos, ":");
        }
        pos += snprintf(buffer + pos, bufferLen - pos, "%02X%02X", addr[2 * group], addr[2 * group + 1]);
    }
}

void safePrint(const char *format, ...) {
    if (Serial) {
        char buffer[MAX_PAYLOAD_SIZE]; // Adjust the buffer size as needed.
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        Serial.print(buffer);
    }
}

void safePrintln(const char *format, ...) {
    if (Serial) {
        char buffer[MAX_PAYLOAD_SIZE]; // Adjust the buffer size as needed.
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        Serial.println(buffer);
    }
}

void sendIPv6Message(const uint8_t *srcAddr, const uint8_t *destAddr, const uint8_t *message, size_t msgLen, uint8_t repeatCount) {
    if (!meshRadio) {
        return;
    }

    // Calculate number of fragments required.
    safePrintln("Message length: %d", msgLen);
    int fragCount = (msgLen + MAX_PAYLOAD_SIZE - 1) / MAX_PAYLOAD_SIZE;
    safePrintln("Sending message with %d fragments", fragCount);
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

bool reassembleFragment(const IPv6Packet *packet, char *outMessage, size_t outMessageSize, uint8_t *actualLength) {
    ReassemblyContext *ctx = getReassemblyContext(packet);
    if (ctx == NULL) {
        // No available context.
        return false;
    }

    // Update the last update time.
    ctx->lastUpdate = millis();

    // Validate fragment index.
    if (packet->fragInfo.fragmentIndex >= MAX_FRAGMENTS) {
        return false;
    }

    // If already received, ignore duplicate.
    if (ctx->received[packet->fragInfo.fragmentIndex]) {
        return false;
    }

    // Calculate the storage slot offset for this fragment.
    int slot = packet->fragInfo.fragmentIndex;
    if ((slot * MAX_PAYLOAD_SIZE) + packet->payloadLength > MAX_MESSAGE_SIZE) {
        return false;
    }

    // Store the fragment in its fixed slot.
    memcpy(ctx->buffer + slot * MAX_PAYLOAD_SIZE, packet->payload, packet->payloadLength);
    ctx->fragmentLengths[slot] = packet->payloadLength;
    ctx->received[slot] = true;

    // Check if all expected fragments have been received.
    bool complete = true;
    for (int i = 0; i < ctx->expectedFragments; i++) {
        if (!ctx->received[i]) {
            complete = false;
            break;
        }
        *actualLength = ctx->fragmentLengths[i];
    }

    if (complete) {
        // Compute total length from individual fragment lengths.
        int totalLength = 0;
        for (int i = 0; i < ctx->expectedFragments; i++) {
            totalLength += ctx->fragmentLengths[i];
        }
        if ((size_t)totalLength >= outMessageSize) {
            return false;
        }

        // Copy each fragment's data sequentially into the outMessage buffer.
        int outOffset = 0;
        for (int i = 0; i < ctx->expectedFragments; i++) {
            memcpy(outMessage + outOffset, ctx->buffer + i * MAX_PAYLOAD_SIZE, ctx->fragmentLengths[i]);
            outOffset += ctx->fragmentLengths[i];
        }

        // Clear the context after successful reassembly.
        ctx->inUse = false;
        return true;
    }

    return false;
}
