#include "node.h"

//
// Node.cpp - LoRa IPv6 Mesh Node
//
// Constructor initializes radio with board-specific pin settings.
Node::Node(uint64_t ipv6_first, uint64_t ipv6_last) : radio(RFM95_CS, RFM95_INT) {
    this->ipv6_first = ipv6_first;
    this->ipv6_last = ipv6_last;
}

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

    // Initialize this node's IPv6 address using ipv6_first and ipv6_last.
    toIPv6Address(ipv6_first, ipv6_last, nodeAddress);
}

uint8_t *Node::getIPV6() { return nodeAddress; }

void Node::sendMessage(const uint8_t *message, size_t messageLen, const uint8_t *destAddr, uint8_t repeatCount) {
    char dest_add[48];
    ipv6ToString(destAddr, dest_add, sizeof(dest_add));
    char src_add[48];
    ipv6ToString(nodeAddress, src_add, sizeof(src_add));
    safePrintln("Sending binary message to %s from %s", dest_add, src_add);
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

                    char reassembledMessage[MAX_MESSAGE_SIZE];
                    bool complete = reassembleFragment(&incomingPacket, reassembledMessage, sizeof(reassembledMessage));
                    if (complete) {
                        safePrintln("Reassembled message from %s: %s", addrStr, reassembledMessage);
                    } else {
                        safePrintln("Fragment %u/%u from %s", incomingPacket.fragInfo.fragmentIndex, incomingPacket.fragInfo.fragmentCount, addrStr);
                    }
                } else if (ipv6Equal(incomingPacket.destination, IGNORE_ADDRESS)) {
                    // Ignore this packet.
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

// Returns a pointer to the radio instance.
RH_RF95 *Node::getRadio() { return &radio; }

bool Node::readSerialBinary(uint8_t *dest, uint8_t *payload, size_t *pPayloadLen) {
    // Define our states.
    enum State { WAIT_FOR_LENGTH, WAIT_FOR_ADDRESS, WAIT_FOR_PAYLOAD };
    // Static variables persist between calls.
    static State state = WAIT_FOR_LENGTH;
    static uint16_t payloadLength = 0; // Payload length as read from header.
    static uint16_t index = 0;
    // Buffer to hold the destination plus payload.
    // Maximum: 16 bytes (address) + 4096 bytes (payload) = 4112 bytes.
    const size_t bufferSize = IPV6_ADDR_LEN + 4096;
    static uint8_t buffer[bufferSize];

    // Process available serial bytes.
    while (Serial.available() > 0) {
        if (state == WAIT_FOR_LENGTH) {
            // Wait until we can read 2 bytes for the payload length.
            if (Serial.available() >= 2) {
                uint16_t high = (uint8_t)Serial.read();
                uint16_t low = (uint8_t)Serial.read();
                payloadLength = (high << 8) | low;
                // Check that the payload length does not exceed the maximum (4096).
                if (payloadLength > 4096) {
                    // Invalid header, reset state.
                    state = WAIT_FOR_LENGTH;
                    index = 0;
                    continue;
                }
                // Reset index for new packet.
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
                break; // Not enough address bytes yet.
            }
        }
        if (state == WAIT_FOR_PAYLOAD) {
            // In Option 2, we expect payloadLength bytes of payload.
            // Since index already holds IPV6_ADDR_LEN bytes,
            // we continue until total index equals IPV6_ADDR_LEN + payloadLength.
            while (Serial.available() > 0 && index < (IPV6_ADDR_LEN + payloadLength)) {
                buffer[index++] = Serial.read();
            }
            if (index >= (IPV6_ADDR_LEN + payloadLength)) {
                // Complete packet received.
                // Copy out the destination IPv6 address.
                memcpy(dest, buffer, IPV6_ADDR_LEN);
                // Write the actual payload length.
                *pPayloadLen = payloadLength;
                // Copy out the payload (if any).
                if (payload && payloadLength > 0) {
                    memcpy(payload, buffer + IPV6_ADDR_LEN, payloadLength);
                }
                // Reset state for the next packet.
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
