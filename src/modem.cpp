#include "modem.h"

Modem::Modem()
    : radio(RFM95_CS, RFM95_INT), locator(0), spreading(9), coding(5),
      bandwidth(125), state(MELODI_RADIO_STATE_IDLE),
      fault(MELODI_RADIO_FAULT_NONE), lastStatus(0), radioReady(false)
{
    memset(queue, 0, sizeof(queue));
    memset(domain, 0, sizeof(domain));
}

void Modem::begin()
{
    Serial.begin(115200);
    melodi_radio_stream_init(&stream);
    pinMode(RFM95_RST, OUTPUT);
    digitalWrite(RFM95_RST, HIGH);
    delay(10);
    digitalWrite(RFM95_RST, LOW);
    delay(10);
    digitalWrite(RFM95_RST, HIGH);
    delay(10);
    radioReady = radio.init();
    if (radioReady)
        radio.setPromiscuous(true);
    else
        fault = MELODI_RADIO_FAULT_HARDWARE;
}

uint16_t Modem::queueFree() const
{
    uint16_t free = 0;

    for (uint16_t index = 0; index < MELODI_QUEUE_DEPTH; index++)
        if (!queue[index].active)
            free++;
    return free;
}

void Modem::sendMessage(uint8_t type, const void *payload, size_t length)
{
    uint8_t framed[MELODI_RADIO_PAYLOAD_MAX + MELODI_RADIO_HEADER_SIZE];
    size_t framed_length;

    if (melodi_radio_stream_encode(type, payload, length, framed,
                                   sizeof(framed), &framed_length))
        return;
    Serial.write(framed, framed_length);
}

void Modem::sendStatus()
{
    struct melodi_radio_status status;
    uint8_t message[MELODI_RADIO_PAYLOAD_MAX];
    size_t length;

    memset(&status, 0, sizeof(status));
    status.locator = locator;
    status.queue_depth = MELODI_QUEUE_DEPTH;
    status.queue_free = queueFree();
    status.state = state;
    status.fault = fault;
    if (melodi_radio_encode_status(&status, message, sizeof(message),
                                   &length))
        return;
    Serial.write(message, length);
    lastStatus = millis();
}

void Modem::sendResult(uint32_t cookie, uint32_t duration_us, uint8_t result)
{
    struct melodi_radio_result_report report;
    uint8_t message[MELODI_RADIO_PAYLOAD_MAX];
    size_t length;

    memset(&report, 0, sizeof(report));
    report.cookie = cookie;
    report.duration_us = duration_us;
    report.result = result;
    if (melodi_radio_encode_result(&report, message, sizeof(message),
                                   &length))
        return;
    Serial.write(message, length);
}

void Modem::fail(uint8_t reason)
{
    state = MELODI_RADIO_STATE_FAILED;
    fault = reason;
    sendStatus();
}

void Modem::handleIdentify()
{
    struct melodi_radio_info info;
    uint8_t message[MELODI_RADIO_PAYLOAD_MAX];
    size_t length;

    memset(&info, 0, sizeof(info));
    info.abi_version = MELODI_RADIO_VERSION;
    info.packet_mtu = MELODI_RADIO_PACKET_MAX;
    info.queue_depth = MELODI_QUEUE_DEPTH;
    strncpy(info.firmware, MELODI_FIRMWARE, sizeof(info.firmware) - 1);
    strncpy(info.hardware, MELODI_BOARD, sizeof(info.hardware) - 1);
    if (melodi_radio_encode_info(&info, message, sizeof(message), &length))
        return;
    Serial.write(message, length);
}

bool Modem::applyRadio(const struct melodi_radio_configure *config)
{
    if (!radioReady)
        return false;
    if (!radio.setFrequency((float)config->frequency_hz / 1000000.0f))
        return false;
    radio.setSignalBandwidth((long)config->bandwidth_khz * 1000L);
    radio.setSpreadingFactor(config->spreading_factor);
    radio.setCodingRate4(config->coding_rate);
    radio.setTxPower(config->transmit_power_dbm, false);
    radio.setModeRx();
    return true;
}

void Modem::handleConfigure(const uint8_t *payload, uint16_t length)
{
    struct melodi_radio_configure config;

    if (melodi_radio_decode_configure(payload, length, &config)) {
        fail(MELODI_RADIO_FAULT_DOMAIN);
        return;
    }
    if (!applyRadio(&config)) {
        fail(MELODI_RADIO_FAULT_HARDWARE);
        return;
    }
    locator = config.locator;
    spreading = config.spreading_factor;
    coding = config.coding_rate;
    bandwidth = config.bandwidth_khz;
    memcpy(domain, config.domain, sizeof(domain));
    memset(queue, 0, sizeof(queue));
    state = MELODI_RADIO_STATE_READY;
    fault = MELODI_RADIO_FAULT_NONE;
    sendStatus();
}

void Modem::handleTransmit(const uint8_t *payload, uint16_t length)
{
    struct melodi_radio_transmit request;
    uint16_t index;

    if (melodi_radio_decode_transmit(payload, length, &request))
        return;
    if (state != MELODI_RADIO_STATE_READY) {
        sendResult(request.cookie, 0, MELODI_RADIO_RESULT_NOT_READY);
        return;
    }
    for (index = 0; index < MELODI_QUEUE_DEPTH; index++)
        if (!queue[index].active)
            break;
    if (index == MELODI_QUEUE_DEPTH) {
        sendResult(request.cookie, 0, MELODI_RADIO_RESULT_BUSY);
        return;
    }
    queue[index].cookie = request.cookie;
    queue[index].destination = request.destination;
    queue[index].length = request.payload_length;
    memcpy(queue[index].payload, request.payload, request.payload_length);
    queue[index].active = true;
}

void Modem::handleReset()
{
    memset(queue, 0, sizeof(queue));
    locator = 0;
    state = MELODI_RADIO_STATE_IDLE;
    fault = MELODI_RADIO_FAULT_NONE;
    if (radioReady)
        radio.setModeIdle();
    sendStatus();
}

void Modem::handleMessage(const struct melodi_radio_header *header,
                          const uint8_t *payload)
{
    switch (header->type) {
    case MELODI_RADIO_T_IDENTIFY:
        handleIdentify();
        break;
    case MELODI_RADIO_T_CONFIGURE:
        handleConfigure(payload, header->length);
        break;
    case MELODI_RADIO_T_TRANSMIT:
        handleTransmit(payload, header->length);
        break;
    case MELODI_RADIO_T_RESET:
        handleReset();
        break;
    default:
        break;
    }
}

void Modem::servicePending()
{
    uint8_t frame[MELODI_OTA_HEADER + MELODI_RADIO_PACKET_MAX];
    unsigned long started;
    uint32_t duration_us;
    uint16_t index;

    if (state != MELODI_RADIO_STATE_READY)
        return;
    for (index = 0; index < MELODI_QUEUE_DEPTH; index++) {
        if (!queue[index].active)
            continue;
        frame[0] = (uint8_t)(queue[index].destination >> 24);
        frame[1] = (uint8_t)(queue[index].destination >> 16);
        frame[2] = (uint8_t)(queue[index].destination >> 8);
        frame[3] = (uint8_t)queue[index].destination;
        frame[4] = (uint8_t)(locator >> 24);
        frame[5] = (uint8_t)(locator >> 16);
        frame[6] = (uint8_t)(locator >> 8);
        frame[7] = (uint8_t)locator;
        memcpy(frame + MELODI_OTA_HEADER, queue[index].payload,
               queue[index].length);
        started = micros();
        if (!radio.send(frame, MELODI_OTA_HEADER + queue[index].length)) {
            sendResult(queue[index].cookie, 0,
                       MELODI_RADIO_RESULT_TOO_LARGE);
            queue[index].active = false;
            continue;
        }
        radio.waitPacketSent();
        duration_us = (uint32_t)(micros() - started);
        radio.setModeRx();
        sendResult(queue[index].cookie, duration_us,
                   MELODI_RADIO_RESULT_SENT);
        queue[index].active = false;
        return;
    }
}

void Modem::serviceRadio()
{
    uint8_t frame[MELODI_OTA_HEADER + MELODI_RADIO_PACKET_MAX];
    struct melodi_radio_receive receive;
    uint8_t message[MELODI_RADIO_PAYLOAD_MAX];
    uint8_t length = sizeof(frame);
    size_t encoded;

    if (state != MELODI_RADIO_STATE_READY || !radio.available())
        return;
    if (!radio.recv(frame, &length))
        return;
    if (length <= MELODI_OTA_HEADER)
        return;
    memset(&receive, 0, sizeof(receive));
    receive.destination = (uint32_t)frame[0] << 24 |
                          (uint32_t)frame[1] << 16 |
                          (uint32_t)frame[2] << 8 | frame[3];
    receive.source = (uint32_t)frame[4] << 24 | (uint32_t)frame[5] << 16 |
                     (uint32_t)frame[6] << 8 | frame[7];
    if (!receive.source || !receive.destination)
        return;
    if (receive.destination != MELODI_RADIO_LOCATOR_BROADCAST &&
        receive.destination != locator)
        return;
    receive.rssi = (int16_t)radio.lastRssi();
    receive.snr = (int16_t)radio.lastSNR();
    receive.hops = 0;
    receive.payload = frame + MELODI_OTA_HEADER;
    receive.payload_length = (uint16_t)(length - MELODI_OTA_HEADER);
    if (melodi_radio_encode_receive(&receive, message, sizeof(message),
                                    &encoded))
        return;
    Serial.write(message, encoded);
}

void Modem::poll()
{
    const struct melodi_radio_header *header;
    const uint8_t *payload;

    while (Serial.available() > 0) {
        int byte = Serial.read();

        if (byte < 0)
            break;
        if (melodi_radio_stream_feed(&stream, (uint8_t)byte, &header,
                                     &payload) == 1)
            handleMessage(header, payload);
    }
    serviceRadio();
    servicePending();
    if (millis() - lastStatus >= MELODI_STATUS_INTERVAL_MS)
        sendStatus();
}
