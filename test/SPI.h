#ifndef SPI_STUB_H
#define SPI_STUB_H
class SPIStub {
  public:
    void setSCK(int) {}
    void setRX(int) {}
    void setTX(int) {}
};
extern SPIStub SPI;
#endif
