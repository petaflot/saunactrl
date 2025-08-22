#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <PID_v1.h>
#include <LittleFS.h>

// this will enable serial debugging output
//#define SINGLEPHASE_TESTMODE

const char* ssid = "engrenage";
const char* password = "3n9r3na93";
uint8_t bssid[] = { 0x00, 0x1D, 0x7E, 0xFA, 0xF5, 0x2A };
// Static IP configuration
IPAddress local_IP(10, 11, 21, 33);
IPAddress subnet(255, 255, 255, 0);
IPAddress gateway(10, 11, 21, 13);
IPAddress dns(10, 11, 12, 13);

//#define WS2812_Din	D0
#define ONE_WIRE_BUS	D1      // GPIO for temperature sensors
//#define RAON		D2
#define RELAY1 		D3
#define	LED		D4
//#define	SCLK		D5
//#define SDO		D6
//#define	SDI		D7
//#define	LATCH		D8
#ifndef SINGLEPHASE_TESTMODE
  #define RELAY2 		3	// RX/TX TODO which one?
  #define RELAY3 		1	// TX/RX TODO which one?
#endif
//#define AOUTA		A0

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
DeviceAddress sensor1;

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// PID setup
double Setpoint = 105.0, Input = 0, Output = 0;
PID myPID(&Input, &Output, &Setpoint, 2, 5, 1, DIRECT);

// Relay control
bool enabled = false;
unsigned long lastSend = 0;

void notifyClients() {
  char msg[64];
  snprintf(msg, sizeof(msg), "{\"temp\":%.2f,\"target\":%.2f,\"enabled\":%s}", Input, Setpoint, enabled ? "true" : "false");
  ws.textAll(msg);
  Serial.println(String("Sent to client: ") + msg);
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
  AwsFrameInfo *info = (AwsFrameInfo*)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
    String msg;
    for (size_t i = 0; i < len; i++) {
      msg += (char)data[i];
    }
    Serial.println("Received WebSocket message: " + msg);
    if (msg == "enable") {
      enabled = true;
      Serial.println("enabling");
    } else if (msg == "disable") {
      enabled = false;
      Serial.println("disabling");
    } else if (msg.startsWith("target:")) {
      Setpoint = msg.substring(7).toFloat();
    }
    notifyClients();
  }
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
             void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    Serial.printf("Client connected: #%u\n", client->id());
    notifyClients();
  } else if (type == WS_EVT_DATA) {
    handleWebSocketMessage(arg, data, len);
  }
}

void setup() {

  pinMode(LED, OUTPUT);
  pinMode(RELAY1, OUTPUT);
#ifdef SINGLEPHASE_TESTMODE
  Serial.begin(115200);
  delay(1000);
#else
  pinMode(RELAY2, OUTPUT);
  pinMode(RELAY3, OUTPUT);
  // redundant, HIGH at boot
  digitalWrite(RELAY2, HIGH);
  digitalWrite(RELAY3, HIGH);
#endif
  digitalWrite(RELAY1, LOW);

  sensors.begin();
  if (!sensors.getAddress(sensor1, 0)) {
#ifdef SINGLEPHASE_TESTMODE
    Serial.println("No temperature sensor found");
#endif
    //return;
  } else {
#ifdef SINGLEPHASE_TESTMODE
    Serial.print("Sensor 0 address: ");
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
WiFi.begin(ssid, password, 0, bssid);
//WiFi.begin(ssid, password);

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
  myPID.SetOutputLimits(0, 3); // 3 relays max

  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

  ws.onEvent(onEvent);
  server.addHandler(&ws);
  server.begin();
#ifdef SINGLEPHASE_TESTMODE
  Serial.println("HTTP server started");
#endif
}

void loop() {
  sensors.requestTemperatures();
  Input = sensors.getTempC(sensor1);

  if (enabled) {
  	digitalWrite(LED, HIGH);
  } else {
  	digitalWrite(LED, LOW);
  }

  if (Input == DEVICE_DISCONNECTED_C) {
#ifdef SINGLEPHASE_TESTMODE
    Serial.println("Sensor disconnected!");
#endif
    Output = 0;
  } else if (enabled) {
    myPID.Compute();
#ifdef SINGLEPHASE_TESTMODE
    Serial.printf("Temp: %.2f °C, Target: %.2f °C, PID Output: %.2f\n", Input, Setpoint, Output);
#endif
  } else {
    Output = 0;
  }

  digitalWrite(RELAY1, enabled && Output >= 1 ? LOW : HIGH);
#ifndef SINGLEPHASE_TESTMODE
  digitalWrite(RELAY2, enabled && Output >= 2 ? LOW : HIGH);
  digitalWrite(RELAY3, enabled && Output >= 3 ? LOW : HIGH);
#endif

  if (millis() - lastSend > 10000) {
    notifyClients();
    lastSend = millis();
  }

  delay(1000);
}

