#ifndef MODEM_H
#define MODEM_H

#include <Arduino.h>
#include <RH_RF95.h>
#include <SPI.h>

#include "radio.h"

// Board-specific pin definitions.
#if defined(__AVR_ATmega32U4__)
#define RFM95_CS 8
#define RFM95_INT 7
#define RFM95_RST 4
#define MELODI_BOARD "feather-32u4-rfm"
#elif defined(ADAFRUIT_FEATHER_M0) || defined(ADAFRUIT_FEATHER_M0_EXPRESS) || defined(ARDUINO_SAMD_FEATHER_M0)
#define RFM95_CS 8
#define RFM95_INT 3
#define RFM95_RST 4
#define MELODI_BOARD "feather-m0-rfm"
#elif defined(ARDUINO_ADAFRUIT_FEATHER_RP2040_RFM)
#if defined(PIN_RFM_CS)
#define RFM95_CS PIN_RFM_CS
#define RFM95_INT PIN_RFM_DIO0
#define RFM95_RST PIN_RFM_RST
#else
#define RFM95_CS 16
#define RFM95_INT 21
#define RFM95_RST 17
#endif
#define MELODI_BOARD "feather-rp2040-rfm"
#elif defined(LILYGO_TTGO_LORA32_V2)
#define RFM95_CS 18
#define RFM95_INT 26
#define RFM95_RST 23
#define MELODI_BOARD "ttgo-lora32-v2"
#else
#error "no melodi board pin map for this target"
#endif

#define MELODI_FIRMWARE "melodi-fw-0.1.0"
#define MELODI_QUEUE_DEPTH 24
#define MELODI_OTA_HEADER 8
#define MELODI_STATUS_INTERVAL_MS 2000

struct ModemTransmit {
    uint32_t cookie;
    uint32_t destination;
    uint16_t length;
    uint8_t payload[MELODI_RADIO_PACKET_MAX];
    bool active;
};

class Modem {
  public:
    Modem();
    void begin();
    void poll();

  private:
    void handleMessage(const struct melodi_radio_header *header,
                       const uint8_t *payload);
    void handleIdentify();
    void handleConfigure(const uint8_t *payload, uint16_t length);
    void handleTransmit(const uint8_t *payload, uint16_t length);
    void handleReset();
    void servicePending();
    void serviceRadio();
    void sendStatus();
    void sendResult(uint32_t cookie, uint32_t duration_us, uint8_t result);
    void sendMessage(uint8_t type, const void *payload, size_t length);
    void fail(uint8_t fault);
    bool applyRadio(const struct melodi_radio_configure *config);
    uint16_t queueFree() const;

    RH_RF95 radio;
    struct melodi_radio_stream stream;
    struct ModemTransmit queue[MELODI_QUEUE_DEPTH];
    uint32_t locator;
    uint8_t domain[MELODI_RADIO_DOMAIN_SIZE];
    uint8_t spreading;
    uint8_t coding;
    uint16_t bandwidth;
    uint8_t state;
    uint8_t fault;
    unsigned long lastStatus;
    bool radioReady;
};

#endif
