#include "node.h"
#include <string>

#define MY_NODE_ID 30

unsigned long previousMillis = 0;
const long interval = 5000; // Broadcast every 5 seconds.

Node node(MY_NODE_ID);

void setup() {
    node.init();
    node.safePrintln("LoRa IPv6 Mesh Node Initialized");
}

void loop() {
    unsigned long currentMillis = millis();

    std::string message = "This is a long message that exceeds the 20 byte payload size and must be fragmented and reassembled adn was sent from node 30";

    if (currentMillis - previousMillis > interval) {
        previousMillis = currentMillis;
        node.broadcastMessage(message.c_str(), 1);
    }

    node.poll();
}
