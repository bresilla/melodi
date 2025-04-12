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

//  broadcastMessage sends a message to the broadcast address.
void Node::broadcastMessage(const char *message, uint8_t repeatCount) { sendIPv6Message(nodeAddress, BROADCAST_ADDRESS, message, repeatCount); }

// sendMessage sends a message to a specific destination address.
void Node::sendMessage(const char *message, const uint8_t *destAddr, uint8_t repeatCount) { sendIPv6Message(nodeAddress, destAddr, message, repeatCount); }

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

// Safe printing functions that check if Serial is available.
void Node::safePrint(const char *msg) {
    if (Serial) {
        Serial.print(msg);
    }
}

void Node::safePrintln(const char *msg) {
    if (Serial) {
        Serial.println(msg);
    }
}

// Returns a pointer to the radio instance.
RH_RF95 *Node::getRadio() { return &radio; }
