#ifndef ARDUINO_STUB_H
#define ARDUINO_STUB_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <vector>
#define OUTPUT 1
#define HIGH 1
#define LOW 0
void pinMode(int, int);
void digitalWrite(int, int);
void delay(unsigned long);
unsigned long millis(void);
unsigned long micros(void);
class SerialStub {
  public:
    std::vector<uint8_t> input;
    std::vector<uint8_t> output;
    size_t cursor = 0;
    void begin(unsigned long) {}
    int available() { return (int)(input.size() - cursor); }
    int read() { return cursor < input.size() ? input[cursor++] : -1; }
    void write(const uint8_t *data, size_t length) {
        for (size_t i = 0; i < length; i++) output.push_back(data[i]);
    }
    void feed(const uint8_t *data, size_t length) {
        for (size_t i = 0; i < length; i++) input.push_back(data[i]);
    }
    void clear() { input.clear(); output.clear(); cursor = 0; }
};
extern SerialStub Serial;
#endif
