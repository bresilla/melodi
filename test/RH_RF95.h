#ifndef RH_RF95_STUB_H
#define RH_RF95_STUB_H
#include <stdint.h>
#include <string.h>
#include <vector>
class RH_RF95 {
  public:
    RH_RF95(int, int) {}
    bool initResult = true;
    bool sendResult = true;
    std::vector<std::vector<uint8_t> > sent;
    std::vector<uint8_t> pending;
    bool init() { return initResult; }
    void setPromiscuous(bool) {}
    bool setFrequency(float f) { frequency = f; return f > 0.0f; }
    void setSignalBandwidth(long b) { bandwidth = b; }
    void setSpreadingFactor(int s) { spreading = s; }
    void setCodingRate4(int c) { coding = c; }
    void setTxPower(int p, bool) { power = p; }
    void setModeRx() {}
    void setModeIdle() {}
    bool send(const uint8_t *data, uint8_t length) {
        if (!sendResult) return false;
        sent.push_back(std::vector<uint8_t>(data, data + length));
        return true;
    }
    void waitPacketSent() {}
    bool available() { return !pending.empty(); }
    bool recv(uint8_t *buffer, uint8_t *length) {
        if (pending.empty()) return false;
        uint8_t copy = (uint8_t)pending.size();
        if (copy > *length) return false;
        memcpy(buffer, pending.data(), copy);
        *length = copy;
        pending.clear();
        return true;
    }
    int lastRssi() { return -95; }
    float lastSNR() { return 7; }
    float frequency = 0;
    long bandwidth = 0;
    int spreading = 0, coding = 0, power = 0;
};
#endif
