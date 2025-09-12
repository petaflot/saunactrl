// vim: et ts=2 number
/*
 * sauna control software (or more generally, climate control)
 *
 * date: 	09-2025
 * author:	JCZD
 */
#include <Arduino.h>
//#include <ArduinoJson.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
//#define ASYNC_TCP_DEBUG 1   // For ESP32
//#define DEBUG_ESP_ASYNC_TCP 1
#define DEBUG_ASYNC_HTTP_WEB_SERVER // For ESP8266
#include <ESPAsyncWebServer.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <PID_v1.h>
#include <LittleFS.h>
#include <network.h>
#include <Ticker.h>

#define RELAY_OPEN HIGH
#define RELAY_CLOSED LOW

// this will enable serial debugging output ; number is index of prefered relay in relayPins (MUST NOT be on RX or TX)
//#define SINGLEPHASE_TESTMODE 0

// should we use some time-window-based PWM (NOT for electromechanical relays!)
#define STAGED_SSRs
#ifdef STAGED_SSRs
#define PIDRANGE 100
#else
#define PIDRANGE 4 // TODO RELAY_COUNT ponderated with relayWatts
#endif

#define TEMP_ABSMAX 125
#define TEMP_ERROR -127.0
const int EEPROM_SIZE = 32;
const int ADDR_SETPOINT = 0;
const int ADDR_RELAYMODES = 1;

#ifndef SINGLEPHASE_TESTMODE
constexpr size_t RELAY_COUNT = 3;
#else
constexpr size_t RELAY_COUNT = 1;
#endif

//#define WS2812_Din	D0
#define ONE_WIRE_BUS	D1      // GPIO for temperature sensors
//#define RAON		D2
#define RELAY1 		D3
#define	DOOR_SW		D4	// NOTE: when on D4, door MUST be open for flashing!!
#define	SCLK		D5	// 74HCT595
//#define 	SDO		D6
#define	SDI		D7	// 74HCT595
#define	LATCH		D8	// 74HCT595
#define AOUTA		A0

#ifndef SINGLEPHASE_TESTMODE
  // on TX/RX
  #define RELAY2 		1
  #define RELAY3 		3
#endif

// PS-MV-RD starts
// ADC calibration
const float ADC_VOLTAGE_REF = 1.0f;   // V (ESP8266 ADC range)
const int ADC_MAX = 1023;             // analogRead max (0..1023)
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

struct ChannelBuffer {
  int samples[BUFFER_SIZE];
  volatile int head = 0;
};

ChannelBuffer chanBuf[16];  // up to 16 channels (8 mux1 + 8 mux2)

Ticker sampler;
int currentChannel = 0;
bool currentMuxIs2 = false;

// ---- Sampling ISR ----
void sampleADC() {
  int raw;
  if (currentMuxIs2) {
    raw = muxSys.readMux2(currentChannel);
  } else {
    raw = muxSys.readMux1(currentChannel);
  }

  ChannelBuffer &buf = chanBuf[currentMuxIs2 ? 8 + currentChannel : currentChannel];
  buf.samples[buf.head] = raw;
  buf.head = (buf.head + 1) % BUFFER_SIZE;
  //Serial.printf("ADC sample chan %i (%i): %i\n", currentChannel, buf.head, raw);

  // move to next channel
  currentChannel++;
  // TODO clean this shit! use a list
  /*if (currentChannel == 0) { currentChannel = 1; }
  if (currentChannel == 1) { currentChannel = 2; }
  if (currentChannel == 2) { currentChannel = 3; }
  if (currentChannel == 3) { currentChannel = 8; }
  if (currentChannel == 8) { currentChannel = 9; }
  if (currentChannel == 9) { currentChannel = 10; }
  if (currentChannel == 10) { currentChannel = 11; }
  if (currentChannel == 11) { currentChannel = 0; }*/

  if ((!currentMuxIs2 && currentChannel >= muxSys.channels1()) ||
      (currentMuxIs2 && currentChannel >= muxSys.channels2())) {
    currentChannel = 0;
    if (!currentMuxIs2 && muxSys.channels2() > 0) {
      currentMuxIs2 = true;
    } else {
      currentMuxIs2 = false;
    }
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
// PS-MV-RD ends


OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
DeviceAddress sensor0, sensor1;

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// PID setup
double Setpoint = 75.0, Input = 0, Output = 0, Ambiant = 0;

// Gains tuned for slow thermal response
double Kp = 5.0;   // stronger proportional action
double Ki = 2.;    // weaker integral to avoid windup
double Kd = 2.0;    // derivative helps damp oscillations

PID myPID(&Input, &Output, &Setpoint, Kp, Ki, Kd, DIRECT);

// Time-proportional control
#ifdef STAGED_SSRs
const unsigned long windowSize = 10000; // 10 seconds
unsigned long windowStartTime = 0;
#endif


// Relay control
bool enabled = false;
bool door_is_open;
unsigned long lastSend = 0;
#ifndef SINGLEPHASE_TESTMODE
const int relayPins[RELAY_COUNT] = { RELAY1, RELAY2, RELAY3 };
#else
const int relayPins[RELAY_COUNT] = { RELAY1 };
#endif
enum RelayModes { RELAY_OFF, RELAY_PID, RELAY_ON };
RelayModes relayModes[RELAY_COUNT] = {};
enum RelayStates { RELAY_IS_OFF, RELAY_IS_ON, SOMETHING_IS_BROKEN };
RelayStates relayStates[RELAY_COUNT] = {};
RelayStates lastRelayStates[RELAY_COUNT] = {};
#ifdef STAGED_SSRs
unsigned long relayDutyCycles[RELAY_COUNT] = {};
#endif

/*
void saveValueToEEPROM(int save_where, double val) {
  EEPROM.put(save_where, val);
  EEPROM.commit();
}
*/

void loadSetpoint() {
  double val;
  EEPROM.get(ADDR_SETPOINT, val);
  if (!isnan(val) && val > 0 && val < TEMP_ABSMAX && val > TEMP_ERROR) {
    Setpoint = val;
  }
}

void loadRelayModes() {
  double val;
  EEPROM.get(ADDR_RELAYMODES, val);
  // TODO
}

class JsonBuilder {
public:
  JsonBuilder() { clear(); }

  void clear() {
    pos = snprintf(buffer, sizeof(buffer), "{");
    first = true;
  }

  // --- Single numeric or enum ---
  template <typename T>
  typename std::enable_if<std::is_arithmetic<T>::value || std::is_enum<T>::value>::type
  addValue(const char *key, T value) {
    if (!first) pos += snprintf(buffer + pos, sizeof(buffer) - pos, ",");
    pos += snprintf(buffer + pos, sizeof(buffer) - pos,
                    "\"%s\":%.3f", key, static_cast<double>(value));
    first = false;
  }

  // --- Single string ---
  void addValue(const char *key, const char *value) {
    if (!first) pos += snprintf(buffer + pos, sizeof(buffer) - pos, ",");
    pos += snprintf(buffer + pos, sizeof(buffer) - pos,
                    "\"%s\":\"%s\"", key, value);
    first = false;
  }
  void addValue(const char *key, const String &value) { addValue(key, value.c_str()); }

  // --- Array of numeric/enums ---
  template <typename T, size_t N>
  void addValue(const char *key, T (&arr)[N]) {
    if (!first) pos += snprintf(buffer + pos, sizeof(buffer) - pos, ",");
    pos += snprintf(buffer + pos, sizeof(buffer) - pos, "\"%s\":[", key);
    for (size_t i = 0; i < N; i++) {
      pos += snprintf(buffer + pos, sizeof(buffer) - pos,
                      (i < N - 1) ? "%d," : "%d", static_cast<int>(arr[i]));
    }
    pos += snprintf(buffer + pos, sizeof(buffer) - pos, "]");
    first = false;
  }

  // --- Array of floats from function pointer ---
  void addValue(const char *key, size_t count, float (*func)(size_t)) {
    if (!first) pos += snprintf(buffer + pos, sizeof(buffer) - pos, ",");
    pos += snprintf(buffer + pos, sizeof(buffer) - pos, "\"%s\":[", key);
    for (size_t i = 0; i < count; i++) {
      float val = func(i);
      pos += snprintf(buffer + pos, sizeof(buffer) - pos,
                      (i < count - 1) ? "%.2f," : "%.2f", val);
    }
    pos += snprintf(buffer + pos, sizeof(buffer) - pos, "]");
    first = false;
  }

  const char* finish() {
    snprintf(buffer + pos, sizeof(buffer) - pos, "}");
    return buffer;
  }

  bool hasValues() const {
    return !first;  // false if nothing added yet
  }

private:
  char buffer[512];
  size_t pos;
  bool first;
};

JsonBuilder jb;

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len, AsyncWebSocketClient *client) {
  AwsFrameInfo *info = (AwsFrameInfo*)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
    String msg;
    for (size_t i = 0; i < len; i++) {
      msg += (char)data[i];
    }
#ifdef SINGLEPHASE_TESTMODE
    Serial.println("Received WebSocket message: " + msg);
#endif
    // broadcast value changes
    if (msg == "enable") {
      enabled = true;
      jb.addValue("enabled", true);
      ws.textAll(jb.finish());
    } else if (msg == "disable") {
      enabled = false;
      jb.addValue("enabled", false);
      ws.textAll(jb.finish());
    } else if (msg.startsWith("target:")) {
      Setpoint = msg.substring(7).toFloat();
      jb.addValue("target", Setpoint);
      ws.textAll(jb.finish());
    } else if (msg.startsWith("relay:")) {
      size_t r = msg.substring(6,7).toInt() - 1; // relay index 0..2
      String mode = msg.substring(8);
      if (r >= 0 && r < RELAY_COUNT) {
        if (mode == "on") relayModes[r] = RELAY_ON;
        else if (mode == "off") relayModes[r] = RELAY_OFF;
        else if (mode == "pid") relayModes[r] = RELAY_PID;
      }
      jb.addValue("relayModes", relayModes);
      ws.textAll(jb.finish());

    // single-client status requests
    } else if (msg == "enabled") {
      jb.addValue("enabled:", enabled ? "true" : "false");
      client->text(jb.finish());
    } else if (msg == "ambiant") {
      jb.addValue("ambiant", Ambiant);
      client->text(jb.finish());
    } else if (msg == "temp") {
      jb.addValue("temp", Input);
      client->text(jb.finish());
    } else if (msg == "door") {
      jb.addValue("door", door_is_open ? "open" : "closed");
      client->text(jb.finish());
    } else if (msg == "relays") {
      jb.addValue("relayModes", relayModes);
      jb.addValue("relayStates", relayStates);
#ifdef STAGED_SSRs
      jb.addValue("relayDutyCycles", relayDutyCycles);
#endif
      client->text(jb.finish());
    }
    jb.clear();
  }
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
             void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    IPAddress ip = client->remoteIP();
//#ifdef SINGLEPHASE_TESTMODE
    //Serial.printf("Client connected: #%u from %s\n", client->id(), ip.toString().c_str());
//#endif
    jb.addValue("client", ip.toString().c_str());
  } else if (type == WS_EVT_DATA) {
    handleWebSocketMessage(arg, data, len, client);
  }
}

// Helper: read N raw samples from the ADC for the given channel within the allotted timeslice
// This function returns RMS voltage (V) computed by removing DC offset: sqrt(mean((v-mean)^2))
float measureChannelRMS_rawVolts(std::function<int()> readRawFunc, uint32_t timeslice_ms) {
  uint32_t start = millis();
  uint32_t end = start + timeslice_ms;

  // accumulate sum and sumsq
  uint64_t sum = 0;
  uint64_t sumSq = 0;
  uint32_t count = 0;

  // Busy-sample until timeslice ends
  while (millis() < end) {
    int raw = readRawFunc(); // 0..ADC_MAX
    if (raw < 0) continue;
    sum += (uint32_t)raw;
    sumSq += (uint32_t)raw * (uint32_t)raw;
    count++;
    // small pause to avoid saturating CPU; this also slows sampling rate
    // keep it small — we'll deliberately not delay too long because we want many samples
    // but give a few microseconds for settling
    delayMicroseconds(50);
  }

  if (count == 0) return 0.0f;

  // convert sums to floats for math
  float meanRaw = (float)sum / (float)count;
  float meanSqRaw = (float)sumSq / (float)count;

  // variance = E[x^2] - (E[x])^2
  float varianceRaw = meanSqRaw - (meanRaw * meanRaw);
  if (varianceRaw < 0.0f) varianceRaw = 0.0f; // clamp numerical noise
  float rmsRaw = sqrt(varianceRaw);

  // convert raw ADC RMS to volts
  float voltsRMS = rmsRaw * (ADC_VOLTAGE_REF / (float)ADC_MAX);

  return voltsRMS;
}

void setup() {
  EEPROM.begin(EEPROM_SIZE);

  pinMode(DOOR_SW, INPUT_PULLUP);
  std::fill_n(relayModes, RELAY_COUNT, RELAY_PID);
#ifdef SINGLEPHASE_TESTMODE
  Serial.begin(115200);
  delay(500);
  std::fill_n(relayStates, RELAY_COUNT, SOMETHING_IS_BROKEN);
  relayStates[SINGLEPHASE_TESTMODE] = RELAY_IS_OFF;
  pinMode(relayPins[SINGLEPHASE_TESTMODE], OUTPUT);
  digitalWrite(relayPins[SINGLEPHASE_TESTMODE], RELAY_OPEN);
#else
  for (size_t i = 0; i < RELAY_COUNT; i++) {
    pinMode(relayPins[i], OUTPUT);
    digitalWrite(relayPins[i], RELAY_OPEN);
  }
#endif
  memcpy(lastRelayStates, relayStates, sizeof(relayStates));

  // load saved parameters from EEPROM
  loadSetpoint();
  loadRelayModes();


  sensors.begin();
  if (!sensors.getAddress(sensor0, 0)) {
#ifdef SINGLEPHASE_TESTMODE
    Serial.println("No temperature sensor found");
#endif
  } else {
#ifdef SINGLEPHASE_TESTMODE
    Serial.print("Sensor 0 address: ");
    for (uint8_t i = 0; i < 8; i++) {
      Serial.printf("%02X", sensor0[i]);
    }
    Serial.println();
#endif
  }

  if (!sensors.getAddress(sensor1, 1)) {
#ifdef SINGLEPHASE_TESTMODE
    Serial.println("No second temperature sensor found");
#endif
    // dangerous, do not run!
    while (1) delay(10000);

  } else {
#ifdef SINGLEPHASE_TESTMODE
    Serial.print("Sensor 1 address: ");
    for (uint8_t i = 0; i < 8; i++) {
      Serial.printf("%02X", sensor1[i]);
    }
    Serial.println();
#endif
  }

  if (!LittleFS.begin()) {
#ifdef SINGLEPHASE_TESTMODE
    Serial.println("Failed to mount LittleFS");
#endif
    return;
  } else {
#ifdef SINGLEPHASE_TESTMODE
    Serial.println("Listing files in LittleFS:");
    Dir dir = LittleFS.openDir("/");
    while (dir.next()) {
      Serial.print("  ");
      Serial.print(dir.fileName());
      Serial.print(" (");
      Serial.print(dir.fileSize());
      Serial.println(" bytes)");
    }
#endif
  }

  /*if (!LittleFS.exists("/index.html")) {
    Serial.println("index.html not found in LittleFS!");
  } else {
    Serial.println("index.html found in LittleFS");
  }*/

if (!WiFi.config(local_IP, gateway, subnet, dns)) {
#ifdef SINGLEPHASE_TESTMODE
  Serial.println("STA failed to configure");
#endif
}
#ifdef bssid
WiFi.begin(ssid, password, 0, bssid);
#else
WiFi.begin(ssid, password);
#endif

#ifdef SINGLEPHASE_TESTMODE
  Serial.print("Connecting to WiFi");
#endif
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
#ifdef SINGLEPHASE_TESTMODE
    Serial.print(".");
#endif
  }
#ifdef SINGLEPHASE_TESTMODE
  Serial.println("\nWiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.print("Device MAC address: ");
  Serial.println(WiFi.macAddress());
  Serial.print("Connected to BSSID: ");
  Serial.println(WiFi.BSSIDstr());
#endif

  myPID.SetMode(AUTOMATIC);
  myPID.SetOutputLimits(0, PIDRANGE); // 3 relays, but non-uniform power outputs

  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
  /*
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    Serial.println("Request received for /");
    try {
      // Your logic here
      request->send(200, "text/plain", "OK");
    } catch (...) {
      Serial.println("Handler crashed!");
      request->send(500, "text/plain", "Internal Server Error");
    }
  });
  */

  ws.onEvent(onEvent);
  server.addHandler(&ws);

  server.onNotFound([](AsyncWebServerRequest *request){
    //if(LittleFS.exists("/404.html")){
    //  request->send(LittleFS, "/404.html", "text/html", false); // TODO write this page (and make it pretty)
    //} else {
      request->send(404, "text/plain", "404: Not Found");
    //}
  });

  server.on("/enable", HTTP_GET, [](AsyncWebServerRequest *request){
    enabled = true;
    request->send(200, "text/plain", "Sauna enabled");
    jb.addValue("enabled", true);
    ws.textAll(jb.finish());
  });
  
  server.on("/disable", HTTP_GET, [](AsyncWebServerRequest *request){
    enabled = false;
    request->send(200, "text/plain", "Sauna disabled");
    jb.addValue("enabled", false);
    ws.textAll(jb.finish());
  });
  
  server.on("/status.json", HTTP_GET, [](AsyncWebServerRequest *request){
    // TODO build response dynamically as per RELAY_COUNT (see ChatGPT "sauanctrl" or notifyDynamic.cpp) ; add relayDutyCycles
    char msg[256];
    snprintf(msg, sizeof(msg),"{\n\
	\"ambiant\":%.2f,\n\
	\"temp\":%.2f,\n\
	\"target\":%.2f,\n\
	\"pid\":%.2f,\n\
	\"enabled\":%s,\n\
	\"door\":\"%s\",\n\
	\"relayModes\":[%d,%d,%d],\n\
	\"relayStates\":[%d,%d,%d]\n\
}",
	Ambiant, Input, Setpoint, Output,
  enabled ? "true" : "false", door_is_open ? "open" : "closed",
	relayModes[0], relayModes[1], relayModes[2],
	relayStates[0], relayStates[1], relayStates[2]);
    request->send(200, "text/plain", msg);
#ifdef SINGLEPHASE_TESTMODE
    Serial.printf("sent status.json to client: %s\n", msg);
#endif
  });

  // simple GET handler: /set?temp=75
  server.on("/set", HTTP_GET, [](AsyncWebServerRequest *request){
    if (request->hasParam("target")) {
      String val = request->getParam("target")->value();
      double newTarget = val.toFloat();
      if (newTarget > 0 && newTarget < TEMP_ABSMAX) {
        Setpoint = newTarget;
	      jb.addValue( "target", Setpoint);
        ws.textAll(jb.finish());
        request->send(200, "text/plain", "Target set to " + String(Setpoint,1));
        Serial.println("New Setpoint: " + String(Setpoint));
      } else {
        request->send(400, "text/plain", "Invalid value");
      }
      if (request->hasParam("save")) {
        EEPROM.put(ADDR_SETPOINT, Setpoint);
      }
    }
    if (request->hasParam("relay")) {
      String msg = request->getParam("relay")->value();

    size_t r = msg.substring(6, 7).toInt() - 1;
    String mode = msg.substring(8);

    if (r >= 0 && r < RELAY_COUNT) {
      if (mode == "on") relayModes[r] = RELAY_ON;
      else if (mode == "off") relayModes[r] = RELAY_OFF;
      else if (mode == "pid") relayModes[r] = RELAY_PID;

      if (request->hasParam("save")) {
        EEPROM.put(ADDR_RELAYMODES, relayModes);
      }

      jb.addValue("relayModes", relayModes);
      ws.textAll(jb.finish());
    }
    if (request->hasParam("save")) {
      EEPROM.commit();
    }
  });

  server.begin();
#ifdef SINGLEPHASE_TESTMODE
  Serial.println("HTTP server started");
#endif
}

void loop() {
  if (door_is_open != digitalRead(DOOR_SW)){
    door_is_open = digitalRead(DOOR_SW);
    jb.addValue("door", door_is_open ? "open" : "closed");
  }

  sensors.requestTemperatures();
  Input = sensors.getTempC(sensor0);
  Ambiant = sensors.getTempC(sensor1);

#ifdef STAGED_SSRs
  for (size_t i = 0; i < RELAY_COUNT; i++) {
    relayDutyCycles[i] = 0;
  }
#endif

  if (enabled && !door_is_open && Input != DEVICE_DISCONNECTED_C) {
    myPID.Compute();
    jb.addValue("pid", Output);
#ifdef SINGLEPHASE_TESTMODE
    Serial.printf("Temp: %.2f °C, Target: %.2f °C, PID Output: %.2f\n", Input, Setpoint, Output);
#endif

#ifndef STAGED_SSRs
    //Serial.println("#ndef STAGED_SSRs");
    // Apply relay states
    // TODO dynamic based on RELAY_COUNT and a (new) relayWatts list

#ifndef SINGLEPHASE_TESTMODE
    for (size_t i = 0; i < RELAY_COUNT; i++) {
	    digitalWrite(relayPins[i], (relayModes[i] == RELAY_ON) ? RELAY_CLOSED :
		    (relayModes[i] == RELAY_PID && Output >= i ? RELAY_CLOSED : RELAY_OPEN));
    }
#else
	  digitalWrite(relayPins[SINGLEPHASE_TESTMODE], (relayModes[SINGLEPHASE_TESTMODE] == RELAY_ON) ? RELAY_CLOSED :
		  (relayModes[SINGLEPHASE_TESTMODE] == RELAY_PID && Output >= 1 ? RELAY_CLOSED : RELAY_OPEN));
#endif
    if (memcmp(relayStates, lastRelayStates, sizeof(relayStates)) != 0) {
      memcpy(lastRelayStates, relayStates, sizeof(relayStates));
      jb.addValue("relayStates", relayStates);
    }
#else
    //Serial.println("#def STAGED_SSRs");
    unsigned long now = millis();
    if (now - windowStartTime > windowSize) {
      windowStartTime += windowSize; // reset window
    }

    // Stage SSRs ; NOTE: this is hardcoded for relative power levels [1,2,1]
    // TODO scale relayDutyCycles values to sth that does not depend of windowSize
    // TODO dynamic based on RELAY_COUNT and a (new) relayWatts list
    if (Output <= 25.) {
      // Stage 1: SSR1 PWM only
      relayDutyCycles[0] = 4*Output;
    } else if (Output <= 75.) {
      // Stage 2: SSR1 full, SSR2 PWM
      relayDutyCycles[0] = 100;
#ifndef SINGLEPHASE_TESTMODE
      relayDutyCycles[1] = Output-25.;
#endif
    } else {
      // Stage 3: SSR1 + SSR2 full, SSR3 PWM
      relayDutyCycles[0] = 100;
#ifndef SINGLEPHASE_TESTMODE
      relayDutyCycles[1] = 100;
      relayDutyCycles[2] = Output-75.;
#endif
    }

    // Apply relay states
#ifndef SINGLEPHASE_TESTMODE
    for (size_t i = 0; i < RELAY_COUNT; i++) {
	    digitalWrite(relayPins[i], (relayModes[i] == RELAY_ON) ? RELAY_CLOSED :
		    (relayModes[i] == RELAY_PID && (now - windowStartTime) < relayDutyCycles[i]*windowSize/PIDRANGE ? RELAY_CLOSED : RELAY_OPEN));
    }
#else
	  digitalWrite(relayPins[SINGLEPHASE_TESTMODE], (relayModes[SINGLEPHASE_TESTMODE] == RELAY_ON) ? RELAY_CLOSED :
		  (relayModes[SINGLEPHASE_TESTMODE] == RELAY_PID && (now - windowStartTime) < relayDutyCycles[SINGLEPHASE_TESTMODE]*windowSize/PIDRANGE ? RELAY_CLOSED : RELAY_OPEN));
#endif
#endif

#ifdef SINGLEPHASE_TESTMODE
  } else if (Input == DEVICE_DISCONNECTED_C) {
    Serial.println("Sensor disconnected!");
#endif
  }

#ifdef SINGLEPHASE_TESTMODE
  const uint8_t ch1_count = muxSys.channels1();
  const uint8_t ch2_count = muxSys.channels2();

  // Partition totalWindowMs equally across all channels you will measure in one scan:
  uint8_t totalChannelsToScan = ch1_count + ch2_count; // scanning all channels both MUX1 and MUX2
  uint32_t timeslice_ms = totalWindowMs / totalChannelsToScan;
  if (timeslice_ms < 10) timeslice_ms = 10; // avoid too-short slices

  Serial.print("Window(ms): "); Serial.print(totalWindowMs);
  Serial.print("  slice(ms): "); Serial.println(timeslice_ms);

  // PS-VM-RD start
  // Read MUX1 direct channels
  /*
   * MUX1 Ch0: input phase brown (231.6 VAC)
   * MUX1 Ch1: input phase black
   * MUX1 Ch2: input phase white
   *
   * MUX2 Ch0: output phase brown
   * MUX2 Ch3: "input" neutral (leftmost upper nicon)
   */
  Serial.println("MUX1 direct RMS (VAC):");
  for (uint8_t ch = 0; ch < ch1_count; ch++) {
    // read lambda calls readMux1(ch) and returns raw integer
    float volts_rms = measureChannelRMS_rawVolts([&]()->int { return muxSys.readMux1(ch); }, timeslice_ms);
    float vac_rms = volts_rms * calibrationMultiplier;
    Serial.printf("  MUX1 Ch%u: %0.3f V RMS   =>  %0.2f VAC\n", ch, volts_rms, vac_rms);
  }

  // Read MUX2 indirectly through MUX1
  Serial.println("MUX2 (via MUX1 channel) RMS (VAC):");
  for (uint8_t ch = 0; ch < ch2_count; ch++) {
    float volts_rms = measureChannelRMS_rawVolts([&]()->int { return muxSys.readMux2(ch); }, timeslice_ms);
    float vac_rms = volts_rms * calibrationMultiplier;
    Serial.printf("  MUX2 Ch%u: %0.3f V RMS   =>  %0.2f VAC\n", ch, volts_rms, vac_rms);
  }
#endif
  // PS-VM-RD end

  if (millis() - lastSend > 10000) {
    jb.addValue("temp", Input);
    jb.addValue("ambiant", Ambiant);
#ifdef STAGED_SSRs
    jb.addValue("relayDutyCycles", relayDutyCycles);
    // PS-VM-RD start
    //jb.addValue("voltages", ...);
#ifdef SINGLEPHASE_TESTMODE
    for (int ch = 0; ch < 16; ch++) {
      float val = computeRMS(ch);
      Serial.printf("Ch%02d: %.2f VAC\n", ch, val);
    }
#endif
    // PS-VM-RD end
    lastSend = millis();
  }

  if (jb.hasValues()){
    ws.textAll(jb.finish());
    jb.clear();
  }

  delay(500);
}

