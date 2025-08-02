#include "node.h"
#include <string>

#ifndef IPV6_FIRST
#define IPV6_FIRST 0xFFFF  // Default fallback
#endif

#ifndef IPV6_LAST
#define IPV6_LAST 0xFFFF   // Default fallback
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
    // Process any incoming serial commands
    if (node.processSerialCommand()) {
        // Command was processed, continue
    }
    
    // Handle LoRa radio polling
    node.poll();
    
    // Small delay to prevent overwhelming the system
    delay(10);
}
