#include "node.h"
#include <string>

#ifndef IPV6_FIRST
#define IPV6_FIRST 0
#endif

#ifndef IPV6_LAST
#define IPV6_LAST 0
#endif

unsigned long previousMillis = 0;
const long interval = 5000; // Broadcast every 5 seconds.

Node node(IPV6_FIRST, IPV6_LAST);

int count = 0;

void setup() {
    node.init();
    safePrintln("LoRa IPv6 Mesh Node Initialized");
}

const std::string message = "This is a long message that exceeds the 20 byte payload size and must be fragmented and reassembled adn was sent from node 30";

void loop() {
    count++;

    uint8_t dest[IPV6_ADDR_LEN];
    size_t payloadLength = 0;
    uint8_t payload[4096];

    if (node.readSerialBinary(dest, payload, &payloadLength)) {
        node.sendMessage(payload, payloadLength, dest, 1);
        safePrintln("We're in the loop %d", count);
    }

    node.poll();
}
