#include "node.h"

//
// Node.cpp - LoRa IPv6 Mesh Node
//
// Constructor initializes radio with board-specific pin settings.
Node::Node(uint8_t nodeId) : radio(RFM95_CS, RFM95_INT), _nodeId(nodeId) {}

// Initialize the node: Serial, Radio, and IPv6 address.
void Node::init() {
    // Initialize Serial at 115200 baud.
    Serial.begin(115200);
    unsigned long startMillis = millis();
    // Wait for Serial for up to 2 seconds.
    while (!Serial && (millis() - startMillis < 2000)) {
        delay(10);
    }

    // Set up radio reset pin.
    pinMode(RFM95_RST, OUTPUT);
    digitalWrite(RFM95_RST, HIGH);

    safePrintln("Initializing LoRa Radio...");

    // Manual reset: pull RFM95_RST low briefly.
    digitalWrite(RFM95_RST, LOW);
    delay(100);
    digitalWrite(RFM95_RST, HIGH);
    delay(100);

    // Loop until the radio is successfully initialized.
    while (!radio.init()) {
        safePrintln("LoRa radio init failed");
        delay(1000);
    }
    safePrintln("LoRa radio init OK!");

    // Set the operating frequency.
    const float RF95_FREQ = 868.0;
    if (!radio.setFrequency(RF95_FREQ)) {
        safePrintln("setFrequency failed");
        while (1) {
        } // Hang in an infinite loop if frequency setup fails.
    }
    // Set TX power.
    radio.setTxPower(23, false);

    // Pass the radio pointer to the mesh layer.
    setRadio(&radio);

    // Initialize this node's IPv6 address using its node ID.
    initIPv6Address(_nodeId, nodeAddress);
}

void Node::broadcastMessage(const char *message, uint8_t repeatCount) {
    safePrintln("Broadcasting message");
    sendIPv6Message(nodeAddress, BROADCAST_ADDRESS, message, repeatCount);
}

void Node::broadcastMessage(const uint8_t *message, size_t messageLen, uint8_t repeatCount) {
    safePrintln("Broadcasting binary message");
    sendIPv6Message(nodeAddress, BROADCAST_ADDRESS, message, messageLen, repeatCount);
}

void Node::sendMessage(const char *message, const uint8_t *destAddr, uint8_t repeatCount) {
    safePrintln("Sending message to %s", destAddr);
    sendIPv6Message(nodeAddress, destAddr, message, repeatCount);
}

void Node::sendMessage(const uint8_t *message, size_t messageLen, const uint8_t *destAddr, uint8_t repeatCount) {
    safePrintln("Sending binary message to %s", destAddr);
    sendIPv6Message(nodeAddress, destAddr, message, messageLen, repeatCount);
}

// poll() listens for incoming packets, filters them,
// reassembles those addressed for this node (or broadcast),
// and forwards others.
void Node::poll() {
    // Wait up to 1 second for an incoming packet.
    if (radio.waitAvailableTimeout(1000)) {
        uint8_t buf[RH_RF95_MAX_MESSAGE_LEN];
        uint8_t len = sizeof(buf);

        if (radio.recv(buf, &len)) {
            // We expect a packet to be the size of IPv6Packet.
            if (len == sizeof(IPv6Packet)) {
                IPv6Packet incomingPacket;
                memcpy(&incomingPacket, buf, sizeof(IPv6Packet));

                char addrStr[48];
                ipv6ToString(incomingPacket.source, addrStr, sizeof(addrStr));

                // Check if the packet is addressed for this node or is broadcast.
                if (ipv6Equal(incomingPacket.destination, nodeAddress) || ipv6Equal(incomingPacket.destination, BROADCAST_ADDRESS)) {

                    char reassembledMessage[256];
                    bool complete = reassembleFragment(&incomingPacket, reassembledMessage, sizeof(reassembledMessage));
                    if (complete) {
                        char outStr[128];
                        snprintf(outStr, sizeof(outStr), "Reassembled message from %s: %s", addrStr, reassembledMessage);
                        safePrintln(outStr);
                    } else {
                        char fragmentInfo[64];
                        snprintf(fragmentInfo, sizeof(fragmentInfo), "Received fragment %u of %u from %s", incomingPacket.fragInfo.fragmentIndex,
                                 incomingPacket.fragInfo.fragmentCount, addrStr);
                        safePrintln(fragmentInfo);
                    }
                } else {
                    // This packet is not destined for us.
                    // Adjust hopLimit (if non-zero) and forward the packet.
                    forwardPacket(&incomingPacket);
                }
            } else {
                safePrintln("Received packet with unexpected length");
            }
        } else {
            safePrintln("Receive failed");
        }
    }
}

void Node::safePrint(const char *format, ...) {
    if (Serial) {
        char buffer[128]; // Adjust the buffer size as needed.
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        Serial.print(buffer);
    }
}

void Node::safePrintln(const char *format, ...) {
    if (Serial) {
        char buffer[128]; // Adjust the buffer size as needed.
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        Serial.println(buffer);
    }
}

// Returns a pointer to the radio instance.
RH_RF95 *Node::getRadio() { return &radio; }

// The actual payload length is written to *pPayloadLen.
bool Node::readSerialBinary(uint8_t *dest, uint8_t *payload, size_t *pPayloadLen) {
    // Define our states.
    enum State { WAIT_FOR_LENGTH, WAIT_FOR_ADDRESS, WAIT_FOR_PAYLOAD };
    // Static variables persist between calls.
    static State state = WAIT_FOR_LENGTH;
    static uint16_t totalExpected = 0; // Total bytes to be received after the length field.
    static uint16_t index = 0;
    // Buffer to hold the destination plus payload.
    // Maximum: 16 bytes (address) + 4096 bytes (payload) = 4112 bytes.
    const size_t bufferSize = IPV6_ADDR_LEN + 4096;
    static uint8_t buffer[bufferSize];

    // Process available serial bytes.
    while (Serial.available() > 0) {
        if (state == WAIT_FOR_LENGTH) {
            // Wait until we can read 2 bytes for totalExpected.
            if (Serial.available() >= 2) {
                uint16_t high = (uint8_t)Serial.read();
                uint16_t low = (uint8_t)Serial.read();
                totalExpected = (high << 8) | low;
                // Validate: at least 16 bytes (destination) and no more than 4112.
                if (totalExpected < IPV6_ADDR_LEN || totalExpected > (IPV6_ADDR_LEN + 4096)) {
                    // Invalid, reset state.
                    state = WAIT_FOR_LENGTH;
                    index = 0;
                    continue;
                }
                index = 0;
                state = WAIT_FOR_ADDRESS;
            } else {
                break; // Not enough data yet.
            }
        }
        if (state == WAIT_FOR_ADDRESS) {
            // Read exactly 16 bytes for the destination IPv6 address.
            while (Serial.available() > 0 && index < IPV6_ADDR_LEN) {
                buffer[index++] = Serial.read();
            }
            if (index >= IPV6_ADDR_LEN) {
                state = WAIT_FOR_PAYLOAD;
            } else {
                break;
            }
        }
        if (state == WAIT_FOR_PAYLOAD) {
            uint16_t payloadLength = totalExpected - IPV6_ADDR_LEN;
            while (Serial.available() > 0 && index < (IPV6_ADDR_LEN + payloadLength)) {
                buffer[index++] = Serial.read();
            }
            if (index >= (IPV6_ADDR_LEN + payloadLength)) {
                // Complete packet received.
                // Copy out destination IPv6 address.
                memcpy(dest, buffer, IPV6_ADDR_LEN);
                // Write payload length.
                if (pPayloadLen) {
                    *pPayloadLen = payloadLength;
                }
                // Copy out payload if any.
                if (payload && payloadLength > 0) {
                    memcpy(payload, buffer + IPV6_ADDR_LEN, payloadLength);
                }
                // Reset state.
                state = WAIT_FOR_LENGTH;
                index = 0;
                return true;
            } else {
                break; // Waiting for more payload bytes.
            }
        }
    }
    return false; // No complete packet available yet.
}
