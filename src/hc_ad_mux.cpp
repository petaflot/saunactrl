#include "hc_ad_mux.h"

uint8_t HC4051::_srState = 0;

HC4051::HC4051(uint8_t muxIndex, uint8_t latchPin, uint8_t clockPin, uint8_t dataPin) {
  _muxIndex = muxIndex;
  _latchPin = latchPin;
  _clockPin = clockPin;
  _dataPin = dataPin;

  pinMode(_latchPin, OUTPUT);
  pinMode(_clockPin, OUTPUT);
  pinMode(_dataPin, OUTPUT);
}

void HC4051::_updateShiftRegister() {
  digitalWrite(_latchPin, LOW);
  shiftOut(_dataPin, _clockPin, MSBFIRST, _srState);
  digitalWrite(_latchPin, HIGH);
}

void HC4051::select(uint8_t channel) {
  if (channel >= 8) return;

  uint8_t base = (_muxIndex == 0) ? 0 : 3;

  // clear bits
  _srState &= ~(0b111 << base);
  // set new bits
  _srState |= ((channel & 0x07) << base);

  _updateShiftRegister();
}

MUXSystem::MUXSystem(uint8_t analogPin, HC4051& mux1, HC4051& mux2, uint8_t mux2_to_mux1_channel)
  : _analogPin(analogPin), _mux1(mux1), _mux2(mux2), _mux2_to_mux1_channel(mux2_to_mux1_channel) {}

int MUXSystem::readMux1(uint8_t channel) {
  if (channel >= 8) return -1;
  _mux1.select(channel);
  delayMicroseconds(5);
  return analogRead(_analogPin);
}

int MUXSystem::readMux2(uint8_t channel) {
  if (channel >= 8) return -1;
  _mux2.select(channel);
  _mux1.select(_mux2_to_mux1_channel);
  delayMicroseconds(5);
  return analogRead(_analogPin);
}

