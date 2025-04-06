#include "node.h"

Node::Node(uint8_t nodeId) : radio(RFM95_CS, RFM95_INT), _nodeId(nodeId) {}

void Node::init() {
    // Initialize Serial.
    Serial.begin(115200);
    unsigned long startMillis = millis();
    while (!Serial && (millis() - startMillis < 2000)) {
        delay(10);
    }

    // Setup radio reset pin.
    pinMode(RFM95_RST, OUTPUT);
    digitalWrite(RFM95_RST, HIGH);

    safePrintln("Initializing LoRa Radio...");

    // Manual reset.
    digitalWrite(RFM95_RST, LOW);
    delay(100);
    digitalWrite(RFM95_RST, HIGH);
    delay(100);

    while (!radio.init()) {
        safePrintln("LoRa radio init failed");
        delay(1000);
    }
    safePrintln("LoRa radio init OK!");

    // Set frequency and TX power.
    const float RF95_FREQ = 868.0;
    if (!radio.setFrequency(RF95_FREQ)) {
        safePrintln("setFrequency failed");
        while (1) {
        }
    }
    radio.setTxPower(23, false);
    // Set the radio pointer in the mesh library.
    setRadio(&radio);
    // Initialize node address.
    initIPv6Address(_nodeId, nodeAddress);
}

void Node::broadcastMessage(const char *message) { sendIPv6Message(nodeAddress, BROADCAST_ADDRESS, message); }

void Node::sendMessage(const char *message, const uint8_t *destAddr) { sendIPv6Message(nodeAddress, destAddr, message); }

void Node::poll() {
    if (radio.waitAvailableTimeout(1000)) {
        uint8_t buf[RH_RF95_MAX_MESSAGE_LEN];
        uint8_t len = sizeof(buf);

        if (radio.recv(buf, &len)) {
            if (len == sizeof(IPv6Packet)) {
                IPv6Packet incomingPacket;
                memcpy(&incomingPacket, buf, sizeof(IPv6Packet));

                char addrStr[48];
                ipv6ToString(incomingPacket.source, addrStr, sizeof(addrStr));

                char reassembledMessage[256];
                bool complete = reassembleFragment(&incomingPacket, reassembledMessage, sizeof(reassembledMessage));
                if (complete) {
                    char outStr[128];
                    snprintf(outStr, sizeof(outStr), "Reassembled message from %s: %s", addrStr, reassembledMessage);
                    safePrintln(outStr);
                } else {
                    char fragmentInfo[64];
                    // Use the new FragInfo fields.
                    snprintf(fragmentInfo, sizeof(fragmentInfo), "Received fragment %u of %u from %s", incomingPacket.fragInfo.fragmentIndex,
                             incomingPacket.fragInfo.fragmentCount, addrStr);
                    safePrintln(fragmentInfo);
                }
            } else {
                safePrintln("Received packet with unexpected length");
            }
        } else {
            safePrintln("Receive failed");
        }
    }
}

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

RH_RF95 *Node::getRadio() { return &radio; }
