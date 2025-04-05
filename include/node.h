#ifndef NODE_H
#define NODE_H

#include "mesh.h"
#include <Arduino.h>
#include <RH_RF95.h>
#include <SPI.h>
#include <Wire.h>

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
    void broadcastMessage(const char *message);
    void poll();
    void safePrint(const char *msg);
    void safePrintln(const char *msg);
    RH_RF95 *getRadio();

  private:
    uint8_t nodeAddress[IPV6_ADDR_LEN];
    RH_RF95 radio; // radio instance constructed with board-specific pins
    uint8_t _nodeId;
};

#endif
