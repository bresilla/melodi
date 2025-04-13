#include "node.h"
#include <string>

#ifndef MY_NODE_ID
#define MY_NODE_ID 0
#endif

unsigned long previousMillis = 0;
const long interval = 5000; // Broadcast every 5 seconds.

Node node(MY_NODE_ID);

int count = 0;

void setup() {
    node.init();
    node.safePrintln("LoRa IPv6 Mesh Node Initialized");
}

const std::string message = "This is a long message that exceeds the 20 byte payload size and must be fragmented and reassembled adn was sent from node 30";

void loop() {
    count++;
    unsigned long currentMillis = millis();

    // if (currentMillis - previousMillis > interval) {
    //     previousMillis = currentMillis;
    //     node.broadcastMessage(message.c_str(), 1);
    // }

    uint8_t dest[IPV6_ADDR_LEN];
    size_t payloadLength = 0;
    uint8_t payload[4096];
    bool received = node.readSerialBinary(dest, payload, &payloadLength);

    char addrNodeStr[48];
    ipv6ToString(node.getIPV6(), addrNodeStr, sizeof(addrNodeStr));

    if (received) {
        char addrDestStr[48];
        ipv6ToString(dest, addrDestStr, sizeof(addrDestStr));
        node.safePrint("Message to ");
        node.safePrintln(addrDestStr);
        node.safePrint(" from ");
        node.safePrintln(addrNodeStr);
        node.sendMessage(payload, sizeof(payload), dest, 2);
    }

    node.safePrintln("Polling for the %u time... from node %u", count, addrNodeStr);

    node.poll();
}
