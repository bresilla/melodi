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
        packet.fragInfo.fragmentTotal = fragCount;  // Total number of fragments.
        packet.fragInfo.sequenceNumber = seq & 0x0F;

        int start = frag * MAX_PAYLOAD_SIZE;
        int remaining = msgLen - start;
        int fragLen = (remaining > MAX_PAYLOAD_SIZE) ? MAX_PAYLOAD_SIZE : remaining;
        packet.payloadLength = fragLen;

        memcpy(packet.source, srcAddr, IPV6_ADDR_LEN);
        memcpy(packet.destination, destAddr, IPV6_ADDR_LEN);
        memcpy(packet.payload, message + start, fragLen);

        safePrintln("Sending binary fragment %d/%d", frag + 1, fragCount);

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

// --- Simplified Reassembly Mechanism ---
#define MAX_REASSEMBLY_CONTEXTS 20
#define REASSEMBLY_TIMEOUT 30000UL // 30 seconds

typedef struct {
    bool active;
    uint8_t source[IPV6_ADDR_LEN];
    uint8_t packetID;
    uint8_t sequence;
    uint8_t totalFragments;
    bool fragmentsReceived[MAX_FRAGMENTS];
    uint8_t fragLengths[MAX_FRAGMENTS];
    char dataBuffer[MAX_MESSAGE_SIZE];
    unsigned long lastUpdate;
} ReassemblyContext;

static ReassemblyContext contexts[MAX_REASSEMBLY_CONTEXTS];

// Resets a given reassembly context to its default (unused) state.
static void resetContext(ReassemblyContext *ctx) {
    ctx->active = false;
    memset(ctx->fragmentsReceived, 0, sizeof(ctx->fragmentsReceived));
    memset(ctx->fragLengths, 0, sizeof(ctx->fragLengths));
    memset(ctx->dataBuffer, 0, sizeof(ctx->dataBuffer));
}

// Finds an existing context that matches the packet’s identifiers,
// or creates and initializes a new one if available.
static ReassemblyContext *findOrCreateContext(const IPv6Packet *pkt) {
    unsigned long now = millis();
    ReassemblyContext *freeCtx = NULL;

    for (int i = 0; i < MAX_REASSEMBLY_CONTEXTS; i++) {
        // Expire old contexts.
        if (contexts[i].active && (now - contexts[i].lastUpdate > REASSEMBLY_TIMEOUT)) {
            resetContext(&contexts[i]);
        }
        // Check for an existing context matching this packet.
        if (contexts[i].active && contexts[i].packetID == pkt->fragInfo.packetID && contexts[i].sequence == pkt->fragInfo.sequenceNumber &&
            ipv6Equal(contexts[i].source, pkt->source)) {
            return &contexts[i];
        }
        // Track the first available free context.
        if (!contexts[i].active && freeCtx == NULL) {
            freeCtx = &contexts[i];
        }
    }

    if (freeCtx) {
        // Initialize free context with packet details.
        freeCtx->active = true;
        memcpy(freeCtx->source, pkt->source, IPV6_ADDR_LEN);
        freeCtx->packetID = pkt->fragInfo.packetID;
        freeCtx->sequence = pkt->fragInfo.sequenceNumber;
        freeCtx->totalFragments = pkt->fragInfo.fragmentTotal;
        freeCtx->lastUpdate = now;
        return freeCtx;
    }
    return NULL; // No context available.
}

// Processes an incoming fragment for reassembly.
// - 'pkt': The received IPv6 packet fragment.
// - 'outMessage': Output buffer for the reassembled message.
// - 'outMsgSize': Size of the output buffer.
// - 'finalLength': Pointer to the variable that will hold the total message length.
bool reassembleFragment(const IPv6Packet *pkt, char *outMessage, size_t outMsgSize, uint8_t *finalLength) {
    ReassemblyContext *ctx = findOrCreateContext(pkt);
    if (ctx == NULL) {
        // No available context.
        return false;
    }

    // Update the context's timestamp.
    ctx->lastUpdate = millis();

    uint8_t fragIdx = pkt->fragInfo.fragmentIndex;
    if (fragIdx >= MAX_FRAGMENTS) {
        return false;
    }
    if (ctx->fragmentsReceived[fragIdx]) {
        // Duplicate fragment.
        return false;
    }

    // Determine where this fragment should be placed.
    int offset = fragIdx * MAX_PAYLOAD_SIZE;
    if (offset + pkt->payloadLength > MAX_MESSAGE_SIZE) {
        return false;
    }
    memcpy(ctx->dataBuffer + offset, pkt->payload, pkt->payloadLength);
    ctx->fragLengths[fragIdx] = pkt->payloadLength;
    ctx->fragmentsReceived[fragIdx] = true;

    // Check if all fragments have been received.
    bool complete = true;
    *finalLength = 0;
    for (uint8_t i = 0; i < ctx->totalFragments; i++) {
        *finalLength += ctx->fragLengths[i];
        if (!ctx->fragmentsReceived[i]) {
            complete = false;
            break;
        }
    }
    if (complete) {
        if (*finalLength >= outMsgSize) {
            return false;
        }
        memcpy(outMessage, ctx->dataBuffer, *finalLength);
        outMessage[*finalLength] = '\0';
        // Clear the context after successful reassembly.
        resetContext(ctx);
        return true;
    }
    return false;
}
