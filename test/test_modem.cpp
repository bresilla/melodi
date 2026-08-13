#include <assert.h>
#include <stdio.h>

#include "modem.h"

SerialStub Serial;
SPIStub SPI;
static unsigned long clock_ms = 0;

void pinMode(int, int) {}
void digitalWrite(int, int) {}
void delay(unsigned long value) { clock_ms += value; }
unsigned long millis(void) { return clock_ms; }
unsigned long micros(void) { return clock_ms * 1000UL; }

static const struct melodi_radio_header *next_message(
    struct melodi_radio_stream *stream, size_t *cursor,
    const uint8_t **payload)
{
    const struct melodi_radio_header *header;

    while (*cursor < Serial.output.size()) {
        int status = melodi_radio_stream_feed(stream,
                                              Serial.output[(*cursor)++],
                                              &header, payload);
        if (status == 1)
            return header;
    }
    return NULL;
}

static void feed_message(uint8_t type, const void *payload, size_t length)
{
    uint8_t framed[MELODI_RADIO_PAYLOAD_MAX + MELODI_RADIO_HEADER_SIZE];
    size_t framed_length;

    assert(melodi_radio_stream_encode(type, payload, length, framed,
                                      sizeof(framed), &framed_length) == 0);
    Serial.feed(framed, framed_length);
}

int main(void)
{
    Modem modem;
    struct melodi_radio_stream reader;
    struct melodi_radio_configure config;
    struct melodi_radio_transmit request;
    struct melodi_radio_status status;
    struct melodi_radio_info info;
    struct melodi_radio_result_report report;
    const struct melodi_radio_header *header;
    const uint8_t *payload;
    uint8_t body[64];
    uint8_t message[MELODI_RADIO_PAYLOAD_MAX];
    size_t cursor = 0;
    size_t length;

    melodi_radio_stream_init(&reader);
    modem.begin();

    /* identify */
    Serial.clear();
    feed_message(MELODI_RADIO_T_IDENTIFY, NULL, 0);
    modem.poll();
    header = next_message(&reader, &cursor, &payload);
    assert(header && header->type == MELODI_RADIO_T_INFO);
    assert(melodi_radio_decode_info(payload, header->length, &info) == 0);
    assert(info.packet_mtu == MELODI_RADIO_PACKET_MAX);
    assert(info.queue_depth == MELODI_QUEUE_DEPTH);
    printf("INFO firmware=%s hardware=%s mtu=%u depth=%u\n", info.firmware,
           info.hardware, info.packet_mtu, info.queue_depth);

    /* configure */
    memset(&config, 0, sizeof(config));
    config.locator = 0x11223344;
    config.frequency_hz = 868100000;
    config.bandwidth_khz = 125;
    config.spreading_factor = 9;
    config.coding_rate = 5;
    config.transmit_power_dbm = 14;
    config.duty_permille = 100;
    assert(melodi_radio_encode_configure(&config, message, sizeof(message),
                                         &length) == 0);
    Serial.feed(message, length);
    modem.poll();
    header = next_message(&reader, &cursor, &payload);
    assert(header && header->type == MELODI_RADIO_T_STATUS);
    assert(melodi_radio_decode_status(payload, header->length, &status) == 0);
    assert(status.state == MELODI_RADIO_STATE_READY);
    assert(status.locator == 0x11223344);
    assert(status.fault == MELODI_RADIO_FAULT_NONE);
    printf("STATUS ready locator=%08x free=%u/%u\n", status.locator,
           status.queue_free, status.queue_depth);

    /* transmit */
    memset(body, 0x5a, sizeof(body));
    memset(&request, 0, sizeof(request));
    request.cookie = 0xabcdef01;
    request.destination = MELODI_RADIO_LOCATOR_BROADCAST;
    request.payload = body;
    request.payload_length = sizeof(body);
    assert(melodi_radio_encode_transmit(&request, message, sizeof(message),
                                        &length) == 0);
    Serial.feed(message, length);
    modem.poll();
    for (;;) {
        header = next_message(&reader, &cursor, &payload);
        if (!header)
            break;
        if (header->type == MELODI_RADIO_T_RESULT) {
            assert(melodi_radio_decode_result(payload, header->length,
                                              &report) == 0);
            assert(report.cookie == 0xabcdef01);
            assert(report.result == MELODI_RADIO_RESULT_SENT);
            printf("RESULT cookie=%08x sent\n", report.cookie);
            break;
        }
    }
    printf("all firmware modem checks passed\n");
    return 0;
}
