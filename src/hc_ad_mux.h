#ifndef HC_AD_MUX_H
#define HC_AD_MUX_H

#include <Arduino.h>

class HC4051 {
  public:
    HC4051(uint8_t muxIndex, uint8_t latchPin, uint8_t clockPin, uint8_t dataPin);

    void select(uint8_t channel);
    uint8_t channels() const { return 8; }

  private:
    uint8_t _muxIndex;   // 0 = bits 0–2, 1 = bits 3–5
    uint8_t _latchPin;
    uint8_t _clockPin;
    uint8_t _dataPin;

    static uint8_t _srState; // shared shift register state
    void _updateShiftRegister();
};

class MUXSystem {
  public:
    MUXSystem(uint8_t analogPin, HC4051& mux1, HC4051& mux2, uint8_t mux2_to_mux1_channel);

    int readMux1(uint8_t channel);           // direct read from MUX1
    int readMux2(uint8_t channel);           // indirect read through MUX1
    uint8_t channels1() const { return 8; }
    uint8_t channels2() const { return 8; }

  private:
    uint8_t _analogPin;
    HC4051& _mux1;
    HC4051& _mux2;
    uint8_t _mux2_to_mux1_channel;
};

#endif

