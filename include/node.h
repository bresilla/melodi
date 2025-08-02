#ifndef NODE_H
#define NODE_H

#include "mesh.h"
#include <Arduino.h>
#include <RH_RF95.h>
#include <SPI.h>
#include <Wire.h>
#include <stdarg.h>

// Board-specific pin definitions.
#if defined(__AVR_ATmega32U4__)
#define RFM95_CS 8
#define RFM95_INT 7
#define RFM95_RST 4
#elif defined(ADAFRUIT_FEATHER_M0) || defined(ADAFRUIT_FEATHER_M0_EXPRESS) || defined(ARDUINO_SAMD_FEATHER_M0)
#define RFM95_CS 8
#define RFM95_INT 3
#define RFM95_RST 4
#elif defined(ARDUINO_ADAFRUIT_FEATHER_RP2040_RFM)
#define RFM95_CS 16
#define RFM95_INT 21
#define RFM95_RST 17
#define SCK 10
#define MISO 12
#define MOSI 11
#elif defined(LILYGO_TTGO_LORA32_V2)
#define RFM95_CS 18
#define RFM95_INT 26
#define RFM95_RST 23
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#endif

// Protocol constants for enhanced serial communication
enum SerialCommand : uint8_t {
    CMD_SEND_MESSAGE = 0x01,
    CMD_SET_IPV6 = 0x02,
    CMD_GET_STATUS = 0x03,
    CMD_SET_CONFIG = 0x04,
    CMD_RESET_NODE = 0x05,
    CMD_GET_NEIGHBORS = 0x06
};

enum ResponseType : uint8_t {
    RESP_ACK = 0x80,
    RESP_NACK = 0x81,
    RESP_STATUS = 0x82,
    RESP_MESSAGE = 0x83,
    RESP_ERROR = 0x84
};

enum ErrorCode : uint8_t {
    ERR_INVALID_COMMAND = 0x01,
    ERR_INVALID_IPV6 = 0x02,
    ERR_RADIO_FAILURE = 0x03,
    ERR_BUFFER_OVERFLOW = 0x04,
    ERR_TIMEOUT = 0x05,
    ERR_CHECKSUM_FAILED = 0x06
};

class Node {
  public:
    Node(uint64_t ipv6_first = IPV6_FIRST, uint64_t ipv6_last = IPV6_LAST);
    void init();
    void sendMessage(const uint8_t *message, size_t messageLen, const uint8_t *destAddr, uint8_t repeatCount);
    void poll();
    RH_RF95 *getRadio();
    uint8_t *getIPV6();
    
    // Enhanced serial protocol methods
    bool processSerialCommand();
    void sendResponse(ResponseType type, const uint8_t* data = nullptr, size_t len = 0);
    bool setIPv6Address(const uint8_t* newAddr);
    void getStatus(uint8_t* statusBuffer);
    void resetToDefaults();

  private:
    RH_RF95 radio;
    uint8_t nodeAddress[IPV6_ADDR_LEN];
    uint64_t ipv6_first;
    uint64_t ipv6_last;
    
    // Enhanced protocol state
    uint8_t currentTxPower;
    float currentFrequency;
    uint8_t currentHopLimit;
    unsigned long startTime;
    bool ipv6SetViaSerial;
    
    // Serial command processing state
    enum CommandState {
        WAIT_FOR_COMMAND,
        WAIT_FOR_DATA
    };
    CommandState cmdState;
    uint8_t currentCommand;
    uint16_t expectedDataLength;
    uint16_t receivedDataLength;
    uint8_t commandBuffer[4096 + 16 + 1]; // Max payload + IPv6 + command byte
    
    // Internal command execution
    bool executeCommand(uint8_t cmd, const uint8_t* data, size_t len);
};

#endif
