#include "node.h"
#include <string>

#ifndef IPV6_FIRST
#define IPV6_FIRST 0xFFFF // Default fallback
#endif

#ifndef IPV6_LAST
#define IPV6_LAST 0xFFFF // Default fallback
#endif

Node node(IPV6_FIRST, IPV6_LAST);

void setup() {
    node.init();

    // Send initial status to let impulse know we're ready
    uint8_t statusBuffer[25];
    node.getStatus(statusBuffer);
    node.sendResponse(RESP_STATUS, statusBuffer, sizeof(statusBuffer));

    safePrintln("LoRa IPv6 Mesh Node Initialized with Enhanced Protocol");
}

void loop() {
    // Process any incoming serial commands with priority
    bool commandProcessed = node.processSerialCommand();

    // If we're in the middle of receiving a command, prioritize serial processing
    if (Serial.available() > 0) {
        // Don't delay if more serial data is available
        return;
    }

    // Handle LoRa radio polling only when no serial data pending
    node.poll();

    // Reduce delay when command was just processed
    if (commandProcessed) {
        delay(1); // Minimal delay after command processing
    } else {
        delay(10); // Normal delay when idle
    }
}
