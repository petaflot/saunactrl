#include <Arduino.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <PID_v1.h>
#include <LittleFS.h>

#define RELAY_OPEN HIGH
#define RELAY_CLOSED LOW

// this will enable serial debugging output
//#define SINGLEPHASE_TESTMODE

#define TEMP_ABSMAX 125
#define TEMP_ERROR -127.0
const int EEPROM_SIZE = 32;
const int ADDR_SETPOINT = 0;


const char* ssid = "engrenage";
const char* password = "3n9r3na93";
uint8_t bssid[] = { 0x00, 0x1D, 0x7E, 0xFA, 0xF5, 0x2A };	// WRT1
IPAddress local_IP(10, 11, 21, 33);
IPAddress gateway(10, 11, 21, 13);
//uint8_t bssid[] = { 0x00, 0x14, 0xBF, 0xA4, 0xE9, 0x6A };	// WRT2
//IPAddress local_IP(10, 11, 22, 33);
//IPAddress gateway(10, 11, 22, 13);

IPAddress subnet(255, 255, 255, 0);
IPAddress dns(10, 11, 12, 13);

//#define WS2812_Din	D0
#define ONE_WIRE_BUS	D1      // GPIO for temperature sensors
//#define RAON		D2
#define RELAY1 		D3
#define	LED		D4
//#define	SCLK		D5
//#define 	SDO		D6
//#define	SDI		D7
//#define	LATCH		D8
#ifndef SINGLEPHASE_TESTMODE
  #define RELAY2 		3	// RX/TX TODO which one?
  #define RELAY3 		1	// TX/RX TODO which one?
#endif
//#define AOUTA		A0

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
DeviceAddress sensor0, sensor1;

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// PID setup
double Setpoint = 75.0, Input = 0, Output = 0, Ambiant = 0;
PID myPID(&Input, &Output, &Setpoint, 2, 5, 1, DIRECT);

// Relay control
bool enabled = false;
unsigned long lastSend = 0;
enum RelayMode { RELAY_OFF, RELAY_PID, RELAY_ON };
RelayMode relayModes[3] = { RELAY_PID, RELAY_PID, RELAY_PID };
enum RelayStates { RELAY_IS_OFF, RELAY_IS_ON, SOMETHING_IS_BROKEN };
RelayStates relayStates[3] = { RELAY_IS_OFF, RELAY_IS_OFF };

void saveValueToEEPROM(int save_where, double val) {
  EEPROM.put(save_where, val);
  EEPROM.commit();
}

void loadSetpoint() {
  double val;
  EEPROM.get(ADDR_SETPOINT, val);
  if (!isnan(val) && val > 0 && val < TEMP_ABSMAX && val > TEMP_ERROR) {
    Setpoint = val;
  }
}


void notifyClients() {
  // TODO remove enabled, target, relayModes
  char msg[128];
  snprintf(msg, sizeof(msg),
           "{\"ambiant\":%.2f,\"temp\":%.2f,\"target\":%.2f,\"pid\":%.2f,\"enabled\":%s,\"relayModes\":[%d,%d,%d],\"relayStates\":[%d,%d,%d]}",
           Ambiant, Input, Setpoint, Output,
	   enabled ? "true" : "false",
           relayModes[0], relayModes[1], relayModes[2],
           relayStates[0], relayStates[1], relayStates[2]);
  ws.textAll(msg);
#ifdef SINGLEPHASE_TESTMODE
  Serial.printf("Sent to client: %s\n", msg);
#endif
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
  AwsFrameInfo *info = (AwsFrameInfo*)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
    String msg;
    for (size_t i = 0; i < len; i++) {
      msg += (char)data[i];
    }
#ifdef SINGLEPHASE_TESTMODE
    Serial.println("Received WebSocket message: " + msg);
#endif
    if (msg == "enable") {
      enabled = true;
#ifdef SINGLEPHASE_TESTMODE
      Serial.println("enabling");
#endif
    } else if (msg == "disable") {
      enabled = false;
#ifdef SINGLEPHASE_TESTMODE
      Serial.println("disabling");
#endif
    } else if (msg.startsWith("target:")) {
      Setpoint = msg.substring(7).toFloat();
    } else if (msg.startsWith("relay:")) {
      int r = msg.substring(6,7).toInt() - 1; // relay index 0..2
      String mode = msg.substring(8);
      if (r >= 0 && r < 3) {
        if (mode == "on") relayModes[r] = RELAY_ON;
        else if (mode == "off") relayModes[r] = RELAY_OFF;
        else if (mode == "pid") relayModes[r] = RELAY_PID;
      }

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
  EEPROM.begin(EEPROM_SIZE);

  pinMode(LED, OUTPUT);
  pinMode(RELAY1, OUTPUT);
#ifdef SINGLEPHASE_TESTMODE
  Serial.begin(115200);
  delay(1000);
#else
  pinMode(RELAY2, OUTPUT);
  pinMode(RELAY3, OUTPUT);
  // redundant (should be HIGH at boot)
  digitalWrite(RELAY2, RELAY_OPEN);
  digitalWrite(RELAY3, RELAY_OPEN);
#endif
  digitalWrite(RELAY1, RELAY_OPEN);

  // load saved setpoint
  loadSetpoint();


  sensors.begin();
  if (!sensors.getAddress(sensor0, 0)) {
#ifdef SINGLEPHASE_TESTMODE
    Serial.println("No temperature sensor found");
#endif
    //return;
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
    //return;
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
  myPID.SetOutputLimits(0, 4); // 3 relays, but non-uniform power outputs!

  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

  ws.onEvent(onEvent);
  server.addHandler(&ws);
  server.begin();
#ifdef SINGLEPHASE_TESTMODE
  Serial.println("HTTP server started");
#endif
  server.on("/enable", HTTP_GET, [](AsyncWebServerRequest *request){
    enabled = true;
    request->send(200, "text/plain", "Sauna enabled");
    notifyClients();
  });
  
  server.on("/disable", HTTP_GET, [](AsyncWebServerRequest *request){
    enabled = false;
    request->send(200, "text/plain", "Sauna disabled");
    notifyClients();
  });
  
  server.on("/status.json", HTTP_GET, [](AsyncWebServerRequest *request){
    char msg[128];
    snprintf(msg, sizeof(msg),
             "{\"temp\":%.2f,\"target\":%.2f,\"pid\":%.2f,\"enabled\":%s,\"relayModes\":[%d,%d,%d],\"relayStates\":[%d,%d,%d]}",
             Input, Setpoint, Output,
             enabled ? "true" : "false",
             relayModes[0], relayModes[1], relayModes[2],
             relayStates[0], relayStates[1], relayStates[2]);
    ws.textAll(msg);
    request->send(200, "text/plain", msg);
  });

  // simple GET handler: /set?temp=75
  server.on("/set", HTTP_GET, [](AsyncWebServerRequest *request){
    if (request->hasParam("temp")) {
      String val = request->getParam("temp")->value();
      double newTarget = val.toFloat();
      if (newTarget > 0 && newTarget < TEMP_ABSMAX) {
        Setpoint = newTarget;
        saveValueToEEPROM(ADDR_SETPOINT, Setpoint);	// TODO only save on explicit request!
        request->send(200, "text/plain", "Target set to " + String(Setpoint,1));
        Serial.println("New Setpoint: " + String(Setpoint));
      } else {
        request->send(400, "text/plain", "Invalid value");
      }
    } else {
      request->send(400, "text/plain", "Missing ?temp=");
    }
  });
}

void loop() {
  sensors.requestTemperatures();
  Input = sensors.getTempC(sensor0);
  Ambiant = sensors.getTempC(sensor1);

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

  if (enabled) {
	digitalWrite(RELAY1, (relayModes[0] == RELAY_ON) ? RELAY_CLOSED :
                       (relayModes[0] == RELAY_PID && Output >= 1 ? RELAY_CLOSED : RELAY_OPEN));
#ifndef SINGLEPHASE_TESTMODE
	digitalWrite(RELAY2, (relayModes[1] == RELAY_ON) ? HIGH :
                       (relayModes[1] == RELAY_PID && Output >= 2 ? RELAY_CLOSED : RELAY_OPEN));
	digitalWrite(RELAY3, (relayModes[2] == RELAY_ON) ? HIGH :
                       (relayModes[2] == RELAY_PID && Output >= 4 ? RELAY_CLOSED : RELAY_OPEN));
#endif
  	digitalWrite(LED, HIGH);
  } else {
  	digitalWrite(LED, LOW);
  	digitalWrite(RELAY1, RELAY_OPEN);
#ifndef SINGLEPHASE_TESTMODE
  	digitalWrite(RELAY2, RELAY_OPEN);
  	digitalWrite(RELAY3, RELAY_OPEN);
#endif
  }

  // TODO abstraction layer and PS-VM-RD compat (remove relayStates[x] above!)
  relayStates[0] = (digitalRead(RELAY1) == HIGH) ? RELAY_IS_OFF : RELAY_IS_ON;
#ifndef SINGLEPHASE_TESTMODE
  relayStates[1] = (digitalRead(RELAY2) == HIGH) ? RELAY_IS_OFF : RELAY_IS_ON;
  relayStates[2] = (digitalRead(RELAY3) == HIGH) ? RELAY_IS_OFF : RELAY_IS_ON;
#else
	relayStates[1] = SOMETHING_IS_BROKEN;
	relayStates[2] = SOMETHING_IS_BROKEN;
#endif

  if (millis() - lastSend > 10000) {
    notifyClients();
    lastSend = millis();
  }

  delay(1000);
}

