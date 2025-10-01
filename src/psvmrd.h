#include <hc_ad_mux.h>
#include <Ticker.h>

#define	SCLK		D5	// 74HCT595
//#define 	SDO		D6
#define	SDI		D7	// 74HCT595
#define	LATCH		D8	// 74HCT595
#define AOUTA		A0


// ADC calibration
const float ADC_VOLTAGE_REF = 1.0f;   // V (ESP8266 ADC range)
const int ADC_MAX = 1023;             // analogRead max (0..1023)
// TODO move this to another file
const float calibrationMultiplier = 875.0f; // approx, may need refining and per-device (and per-channel!) tuning

// Setup MUX1 (bits 0–2) and MUX2 (bits 3–5)
HC4051 mux1(0, LATCH, SCLK, SDI);
HC4051 mux2(1, LATCH, SCLK, SDI);
// System: MUX2 feeds into MUX1 channel 4
MUXSystem muxSys(AOUTA, mux1, mux2, 4);

// Sampling config
/* if sample rate is too high, WiFi starves and watchdog resets device!
 * 250 OK, 500 NOK, 330 OK, 400 starts choking, 350 already chokes
 * then fails at 250.. irregular ping times, I suppose two cores would be better!
 * works so-so, definitely favor two cores there!
 */
const int SAMPLE_RATE_HZ = 250;
const int BUFFER_SIZE = 100;

// TODO move this to another file
constexpr int NUM_CHANNELS = 7;  // up to 16 channels (8 mux1 + 8 mux2)
int adcChannels[NUM_CHANNELS] = {0, 1, 2, 3, 8, 9, 10};  // mux channel numbers to read

struct ChannelBuffer {
  int samples[BUFFER_SIZE];   // big buffer, lives in DRAM
  volatile uint16_t head;     // index, volatile because ISR writes it
};
// One buffer per channel, allocated globally (not on stack)
ChannelBuffer chanBuf[NUM_CHANNELS];

Ticker sampler;
int currentChannel = 0;

// ---- Sampling ISR ----
void sampleADC() {
  int raw;
  if (adcChannels[currentChannel] >= 8) {
    raw = muxSys.readMux2(adcChannels[currentChannel]-8);
  } else {
    raw = muxSys.readMux1(adcChannels[currentChannel]);
  }

  ChannelBuffer &buf = chanBuf[currentChannel];
  buf.samples[buf.head] = raw;
  buf.head = (buf.head + 1) % BUFFER_SIZE;

  if (currentChannel == (NUM_CHANNELS-1)){
    currentChannel = 0;
  } else {
    currentChannel++;
  }
}

// ---- Compute RMS on demand ----
float computeRMS(int chan) {
  noInterrupts();
  ChannelBuffer &buf = chanBuf[chan];
  interrupts();

  double sum = 0, sumSq = 0;
  for (int i = 0; i < BUFFER_SIZE; i++) {
    int v = buf.samples[i];
    sum += v;
    sumSq += (double)v * v;
  }

  double mean = sum / BUFFER_SIZE;
  double variance = (sumSq / BUFFER_SIZE) - (mean * mean);
  if (variance < 0) variance = 0;

  double rmsRaw = sqrt(variance);
  float voltsRMS = rmsRaw * (ADC_VOLTAGE_REF / ADC_MAX);
  return voltsRMS * calibrationMultiplier;
}

void getVoltages(float *out) {
    noInterrupts();
    for (size_t i = 0; i < NUM_CHANNELS; i++) {
        out[i] = computeRMS(i);
    }
    interrupts();
}
float volts[NUM_CHANNELS];
