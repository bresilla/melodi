#include "node.h"

//
// Node.cpp - LoRa IPv6 Mesh Node
//
// Constructor initializes radio with board-specific pin settings.
Node::Node(uint64_t ipv6_first, uint64_t ipv6_last) : radio(RFM95_CS, RFM95_INT) {
    this->ipv6_first = ipv6_first;
    this->ipv6_last = ipv6_last;

    // Initialize enhanced protocol state
    currentTxPower = 23;
    currentFrequency = 868.0;
    currentHopLimit = 10;
    startTime = 0;
    ipv6SetViaSerial = false;

    // Initialize command processing state
    cmdState = WAIT_FOR_COMMAND;
    currentCommand = 0;
    expectedDataLength = 0;
    receivedDataLength = 0;
}

// Initialize the node: Serial, Radio, and IPv6 address.
void Node::init() {
    // CRITICAL: Set buffer sizes BEFORE Serial.begin()
    Serial.setRxBufferSize(4096); // RX buffer for receiving large commands from Python
    Serial.setTxBufferSize(4096); // TX buffer for sending responses back to Python

    // Initialize Serial at 115200 baud
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
    radio.setTxPower(currentTxPower, false);

    // Pass the radio pointer to the mesh layer.
    setRadio(&radio);

    // Initialize this node's IPv6 address using ipv6_first and ipv6_last.
    toIPv6Address(ipv6_first, ipv6_last, nodeAddress);

    // Record start time for status reporting
    startTime = millis();
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
                    createOrUpdateContext(&incomingPacket);
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
    ReassembledPacket completeCtx;
    if (getCompletedContext(&completeCtx)) {
        safePrintln("Reassembled message: %d", completeCtx.payloadLength);
        serialSendReassembledPacket(&completeCtx);
    }
    deleteOldContexts();
}

// Returns a pointer to the radio instance.
RH_RF95 *Node::getRadio() { return &radio; }

// Enhanced Protocol Methods

bool Node::processSerialCommand() {
    static uint8_t commandBuffer[MAX_COMMAND_BUFFER_SIZE]; // Aligned with LoRa limits
    static uint16_t bufferIndex = 0;
    static CommandState state = WAIT_FOR_COMMAND;
    static uint8_t currentCmd = 0;
    static uint16_t expectedLength = 0;
    static unsigned long lastCommandTime = 0;
    static bool skipProcessing = false;

    // Helper function to reset parser state
    auto resetParser = [&]() {
        state = WAIT_FOR_COMMAND;
        bufferIndex = 0;
        expectedLength = 0;
        skipProcessing = true; // Skip next processing cycle to clear buffer
    };

    // Check if we should skip processing to clear buffer after error
    if (skipProcessing) {
        skipProcessing = false;
        // Clear any remaining bytes in buffer
        while (Serial.available() > 0) {
            Serial.read();
        }
        return false;
    }

    // Check for command timeout
    unsigned long now = millis();
    if (state != WAIT_FOR_COMMAND && (now - lastCommandTime > COMMAND_TIMEOUT)) {
        safePrintln("Command timeout: received %d/%d bytes, resetting parser", bufferIndex, expectedLength);
        resetParser();
        return false;
    }

    while (Serial.available() > 0) {
        uint8_t byte = Serial.read();

        switch (state) {
        case WAIT_FOR_COMMAND:
            currentCmd = byte;
            bufferIndex = 0;
            lastCommandTime = now;

            // Determine expected data length based on command
            switch (currentCmd) {
            case CMD_SET_IPV6:
                expectedLength = 16; // IPv6 address
                state = WAIT_FOR_DATA;
                break;

            case CMD_GET_STATUS:
            case CMD_RESET_NODE:
                expectedLength = 0; // No additional data
                return executeCommand(currentCmd, nullptr, 0);

            case CMD_SEND_MESSAGE:
                // Need to read payload length first
                state = WAIT_FOR_DATA;
                expectedLength = 2; // First read payload length
                break;

            case CMD_SET_CONFIG:
                state = WAIT_FOR_DATA;
                expectedLength = 1; // Config type byte
                break;

            default:
                safePrintln("Unknown command: 0x%02x", currentCmd);
                uint8_t errorData[2] = {currentCmd, ERR_INVALID_COMMAND};
                sendResponse(RESP_NACK, errorData, 2);
                resetParser();
                return false;
            }
            break;

        case WAIT_FOR_DATA:
            // CRITICAL FIX: Check buffer bounds before writing
            if (bufferIndex >= MAX_COMMAND_BUFFER_SIZE) {
                safePrintln("Command buffer overflow at index %d", bufferIndex);
                uint8_t errorData[2] = {currentCmd, ERR_BUFFER_OVERFLOW};
                sendResponse(RESP_NACK, errorData, 2);
                resetParser();
                return false;
            }

            commandBuffer[bufferIndex++] = byte;

            // Special handling for CMD_SEND_MESSAGE payload length
            if (currentCmd == CMD_SEND_MESSAGE && bufferIndex == 2) {
                uint16_t payloadLen = (commandBuffer[0] << 8) | commandBuffer[1];

                // CRITICAL FIX: Validate payload size against LoRa limits
                if (payloadLen > MAX_COMMAND_DATA_SIZE) {
                    safePrintln("Payload too large: %d bytes (LoRa max: %d)", payloadLen, MAX_COMMAND_DATA_SIZE);
                    uint8_t errorData[2] = {currentCmd, ERR_MESSAGE_TOO_LARGE};
                    sendResponse(RESP_NACK, errorData, 2);
                    resetParser();
                    return false;
                }

                // Calculate total expected length
                uint32_t totalExpected = 2 + 16 + payloadLen; // length + IPv6 + payload

                // CRITICAL FIX: Check for buffer overflow
                if (totalExpected > MAX_COMMAND_BUFFER_SIZE) {
                    safePrintln("Command too large: %lu bytes (buffer max: %d)", totalExpected, MAX_COMMAND_BUFFER_SIZE);
                    uint8_t errorData[2] = {currentCmd, ERR_BUFFER_OVERFLOW};
                    sendResponse(RESP_NACK, errorData, 2);
                    resetParser();
                    return false;
                }

                expectedLength = (uint16_t)totalExpected;
                safePrintln("Expecting %d total bytes for message", expectedLength);
            }

            if (bufferIndex >= expectedLength) {
                safePrintln("Command complete: received %d/%d bytes", bufferIndex, expectedLength);
                bool success = executeCommand(currentCmd, commandBuffer, bufferIndex);
                resetParser();
                return success;
            }
            break;
        }
    }
    return false;
}

bool Node::executeCommand(uint8_t cmd, const uint8_t *data, size_t len) {
    switch (cmd) {
    case CMD_SET_IPV6:
        if (len == 16) {
            if (setIPv6Address(data)) {
                sendResponse(RESP_ACK, &cmd, 1);
                return true;
            } else {
                uint8_t errorData[2] = {cmd, ERR_INVALID_IPV6};
                sendResponse(RESP_NACK, errorData, 2);
            }
        }
        break;

    case CMD_GET_STATUS: {
        uint8_t statusBuffer[25];
        getStatus(statusBuffer);
        sendResponse(RESP_STATUS, statusBuffer, sizeof(statusBuffer));
        return true;
    }

    case CMD_SEND_MESSAGE:
        if (len >= 18) { // At least 2 bytes length + 16 bytes IPv6
            uint16_t payloadLen = (data[0] << 8) | data[1];
            const uint8_t *destAddr = &data[2];
            const uint8_t *payload = &data[18];

            // CRITICAL FIX: Double-check payload size in execute command
            if (payloadLen > MAX_COMMAND_DATA_SIZE) {
                safePrintln("Execute: Payload too large: %d bytes (max: %d)", payloadLen, MAX_COMMAND_DATA_SIZE);
                uint8_t errorData[2] = {cmd, ERR_MESSAGE_TOO_LARGE};
                sendResponse(RESP_NACK, errorData, 2);
                return false;
            }

            // CRITICAL FIX: Validate actual data length matches expected
            if (len != 18 + payloadLen) {
                safePrintln("Execute: Data length mismatch: expected %d, got %d", 18 + payloadLen, len);
                uint8_t errorData[2] = {cmd, ERR_INVALID_COMMAND};
                sendResponse(RESP_NACK, errorData, 2);
                return false;
            }

            safePrintln("Sending message: %d bytes to destination", payloadLen);
            sendMessage(payload, payloadLen, destAddr, 1);
            sendResponse(RESP_ACK, &cmd, 1);
            return true;
        } else {
            safePrintln("Execute: CMD_SEND_MESSAGE insufficient data: %d bytes", len);
            uint8_t errorData[2] = {cmd, ERR_INVALID_COMMAND};
            sendResponse(RESP_NACK, errorData, 2);
            return false;
        }
        break;

    case CMD_RESET_NODE:
        resetToDefaults();
        sendResponse(RESP_ACK, &cmd, 1);
        return true;

    case CMD_SET_CONFIG:
        // Implementation for configuration changes
        if (len >= 2) {
            uint8_t configType = data[0];
            switch (configType) {
            case 0x01: // TX Power
                if (len >= 2) {
                    currentTxPower = data[1];
                    radio.setTxPower(currentTxPower, false);
                }
                break;
            case 0x02: // Frequency
                if (len >= 5) {
                    uint32_t freq = (data[1] << 24) | (data[2] << 16) | (data[3] << 8) | data[4];
                    currentFrequency = freq / 1000000.0; // Convert Hz to MHz
                    radio.setFrequency(currentFrequency);
                }
                break;
            case 0x03: // Hop Limit
                if (len >= 2) {
                    currentHopLimit = data[1];
                }
                break;
            }
            sendResponse(RESP_ACK, &cmd, 1);
            return true;
        }
        break;
    }

    uint8_t errorData[2] = {cmd, ERR_INVALID_COMMAND};
    sendResponse(RESP_NACK, errorData, 2);
    return false;
}

bool Node::setIPv6Address(const uint8_t *newAddr) {
    // Validate the address (not all zeros, not all 0xFF unless intended)
    bool allZeros = true, allOnes = true;
    for (int i = 0; i < IPV6_ADDR_LEN; i++) {
        if (newAddr[i] != 0x00)
            allZeros = false;
        if (newAddr[i] != 0xFF)
            allOnes = false;
    }

    if (allZeros && !allOnes) {
        return false; // Invalid address
    }

    // Copy new address
    memcpy(nodeAddress, newAddr, IPV6_ADDR_LEN);
    ipv6SetViaSerial = true;

    char addrStr[48];
    ipv6ToString(nodeAddress, addrStr, sizeof(addrStr));
    safePrintln("IPv6 address updated to: %s", addrStr);

    return true;
}

void Node::getStatus(uint8_t *statusBuffer) {
    // [16 bytes: current_ipv6][1 byte: radio_status][1 byte: tx_power]
    // [4 bytes: frequency][1 byte: hop_limit][2 bytes: uptime_seconds]

    memcpy(statusBuffer, nodeAddress, 16);
    statusBuffer[16] = radio.available() ? 0x01 : 0x00; // Radio status
    statusBuffer[17] = currentTxPower;

    uint32_t freq = (uint32_t)(currentFrequency * 1000000); // Convert to Hz
    statusBuffer[18] = (freq >> 24) & 0xFF;
    statusBuffer[19] = (freq >> 16) & 0xFF;
    statusBuffer[20] = (freq >> 8) & 0xFF;
    statusBuffer[21] = freq & 0xFF;

    statusBuffer[22] = currentHopLimit;

    uint16_t uptime = (millis() - startTime) / 1000;
    statusBuffer[23] = (uptime >> 8) & 0xFF;
    statusBuffer[24] = uptime & 0xFF;
}

void Node::sendResponse(ResponseType type, const uint8_t *data, size_t len) {
    const uint8_t HEADER[] = {0xAA, 0xBB, 0xCC, 0xDD};
    Serial.write(HEADER, 4);
    Serial.write((uint8_t)type);
    if (data && len > 0) {
        Serial.write(data, len);
    }
    Serial.flush();
}

void Node::resetToDefaults() {
    // Reset to compile-time defaults
    toIPv6Address(ipv6_first, ipv6_last, nodeAddress);
    currentTxPower = 23;
    currentFrequency = 868.0;
    currentHopLimit = 10;
    ipv6SetViaSerial = false;

    // Apply settings to radio
    radio.setTxPower(currentTxPower, false);
    radio.setFrequency(currentFrequency);

    // Reset command parser state
    resetCommandParser();

    safePrintln("Node reset to defaults");
}

void Node::resetCommandParser() {
    // Reset static variables in processSerialCommand
    // Note: This is a workaround since we can't directly access static variables
    // The actual reset happens in the processSerialCommand timeout mechanism
    safePrintln("Command parser reset requested");
}
