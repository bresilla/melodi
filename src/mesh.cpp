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
        // Fill in the FragInfo structure:
        packet.fragInfo.packetID = currentPacketID; // Packet identifier.
        packet.fragInfo.hopLimit = 10;              // Hop limit (TTL)
        packet.fragInfo.fragmentIndex = frag;       // Fragment index.
        packet.fragInfo.fragmentTotal = fragCount;  // Total number of fragments.

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
    if (packet->fragInfo.hopLimit > 0) {
        packet->fragInfo.hopLimit--;
        meshRadio->send((uint8_t *)packet, sizeof(IPv6Packet));
        meshRadio->waitPacketSent();
    }
}

void createOrUpdateContext(const IPv6Packet *packet) {
    uint8_t fragIndex = packet->fragInfo.fragmentIndex;
    uint8_t fragTotal = packet->fragInfo.fragmentTotal;
    size_t offset = fragIndex * MAX_PAYLOAD_SIZE;
    // safePrintln("Fragment index: %d", fragIndex);
    // safePrintln("Fragment offset: %d", offset);

    // Determine if this is a broadcast packet.
    bool isBroadcast = ipv6Equal(packet->destination, BROADCAST_ADDRESS);

    unsigned long now = millis();
    ReassemblyContext *context = NULL;

    // Search for an existing context that matches this message.
    for (int i = 0; i < MAX_REASSEMBLY_CONTEXTS; i++) {
        if (contexts[i].active && (contexts[i].packetID == packet->fragInfo.packetID) && (contexts[i].broadcast == isBroadcast) &&
            (memcmp(contexts[i].source, packet->source, IPV6_ADDR_LEN) == 0)) {
            context = &contexts[i];
            safePrintln("Found existing context");
            break;
        }
    }

    // If no matching context found, allocate a new one.
    if (context == NULL) {
        safePrintln("Creating new context");
        // Try to find an inactive context.
        for (int i = 0; i < MAX_REASSEMBLY_CONTEXTS; i++) {
            if (!contexts[i].active) {
                context = &contexts[i];
                safePrintln("Found new unused context at index %d", i);
                break;
            }
        }
        // If all contexts are active, reclaim the oldest one.
        if (context == NULL) {
            int oldestIndex = 0;
            unsigned long oldestTime = contexts[0].lastUpdate;
            for (int i = 1; i < MAX_REASSEMBLY_CONTEXTS; i++) {
                if (contexts[i].lastUpdate < oldestTime) {
                    oldestTime = contexts[i].lastUpdate;
                    oldestIndex = i;
                }
            }
            context = &contexts[oldestIndex];
        }
        // // Initialize the new/reclaimed context.
        context->active = true;
        context->broadcast = isBroadcast;
        memcpy(context->source, packet->source, IPV6_ADDR_LEN);
        context->packetID = packet->fragInfo.packetID;
        context->fragmentTotal = fragTotal;
        context->lastFragmentLength = 0; // Will be set when the last fragment is received
        for (int i = 0; i < MAX_FRAGMENTS; i++) {
            context->fragmentsReceived[i] = false;
        }
        context->lastUpdate = now;
        // Clear the data buffer (optional).
        memset(context->dataBuffer, 0, MAX_MESSAGE_SIZE);
        // Add the new fragment to the data buffer.
        memcpy(&context->dataBuffer[offset], packet->payload, packet->payloadLength);
        context->fragmentsReceived[fragIndex] = true;

    } else {
        safePrintln("Updating context");
        context->lastUpdate = now;
        // Compute the offset in the data buffer.
        // Ensure the payload will not overflow the dataBuffer.
        if (offset + packet->payloadLength <= MAX_MESSAGE_SIZE) {
            memcpy(&context->dataBuffer[offset], packet->payload, packet->payloadLength);
            context->fragmentsReceived[fragIndex] = true;
        }
        // // If this is the last fragment, record its length.
        if (fragIndex == fragTotal - 1) {
            context->lastFragmentLength = packet->payloadLength;
            safePrintln("Last fragment received");
        }
        // // Update the last update time.
        context->lastUpdate = now;
    }
}

/*---------------------------------------------------------------------------
  Function: deleteOldContexts
  Description:
    This function checks every active context and disables it if no new
    fragments have been received within the REASSEMBLY_TIMEOUT interval.
---------------------------------------------------------------------------*/
void deleteOldContexts() {
    unsigned long now = millis();

    for (int i = 0; i < MAX_REASSEMBLY_CONTEXTS; i++) {
        if (contexts[i].active && (now - contexts[i].lastUpdate > REASSEMBLY_TIMEOUT)) {
            contexts[i].active = false;
            safePrintln("Context %d timed out", i);
        }
    }
}

/*---------------------------------------------------------------------------
  Function: getCompletedContext
  Description:
    Checks the reassembly contexts for any message that has all of its fragments.
    If found, it copies the reassembled message into the provided
    ReassembledPacket structure, marks the context as inactive, and returns true.
    Otherwise, returns false.

  Note:
    The total reassembled message length is computed as:
         (Number of full fragments * MAX_PAYLOAD_SIZE) + lastFragmentLength.
    Be aware that the ReassembledPacket structure only allocates
    MAX_PAYLOAD_SIZE bytes for its payload. Adjust this as needed.
---------------------------------------------------------------------------*/
bool getCompletedContext(ReassembledPacket *reassembledPacket) {
    bool complete = false;
    for (int i = 0; i < MAX_REASSEMBLY_CONTEXTS; i++) {
        if (contexts[i].active) {
            // Ensure every fragment from 0 to fragmentTotal - 1 is received.
            complete = true;
            for (int j = 0; j < contexts[i].fragmentTotal; j++) {
                bool frag_present = contexts[i].fragmentsReceived[j];
                if (!frag_present) {
                    complete = false;
                    break;
                }
            }
            if (complete) {
                safePrintln("Found complete context");
                // Fill in the reassembled packet.
                reassembledPacket->broadcast = contexts[i].broadcast;
                memcpy(reassembledPacket->source, contexts[i].source, IPV6_ADDR_LEN);

                char src_add[48];
                ipv6ToString(reassembledPacket->source, src_add, sizeof(src_add));
                safePrintln("Reassembled message from %s", src_add);

                // Calculate the full length of the assembled message.
                uint16_t totalLength = ((contexts[i].fragmentTotal - 1) * MAX_PAYLOAD_SIZE) + contexts[i].lastFragmentLength;
                reassembledPacket->payloadLength = totalLength;
                safePrintln("Payload length: %d", reassembledPacket->payloadLength);
                memcpy(reassembledPacket->payload, contexts[i].dataBuffer, totalLength);

                // Mark this context as processed.
                contexts[i].active = false;
                return true;
            }
        }
    }
    return complete;
}
