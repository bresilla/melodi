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

class Node {
  public:
    Node(uint8_t nodeId);
    void init();
    void broadcastMessage(const char *message, uint8_t repeatCount);
    void broadcastMessage(const uint8_t *message, size_t messageLen, uint8_t repeatCount);
    void sendMessage(const char *message, const uint8_t *destAddr, uint8_t repeatCount);
    void sendMessage(const uint8_t *message, size_t messageLen, const uint8_t *destAddr, uint8_t repeatCount);
    void poll();
    void safePrint(const char *format, ...);
    void safePrintln(const char *format, ...);
    RH_RF95 *getRadio();
    bool readSerialBinary(uint8_t *dest, uint8_t *payload, size_t *pPayloadLen);

  private:
    uint8_t nodeAddress[IPV6_ADDR_LEN];
    RH_RF95 radio; // radio instance constructed with board-specific pins
    uint8_t _nodeId;
};

#endif
