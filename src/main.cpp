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

// this will enable serial debugging output
//#define SINGLEPHASE_TESTMODE

#define TEMP_ABSMAX 125
#define TEMP_ERROR -127.0
const int EEPROM_SIZE = 32;
const int ADDR_SETPOINT = 0;
const int ADDR_RELAYMODES = 1;

constexpr size_t RELAY_COUNT = 3;

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
bool door_is_open = true; // just to be safe
unsigned long lastSend = 0;
enum RelayModes { RELAY_OFF, RELAY_PID, RELAY_ON };
RelayModes relayModes[RELAY_COUNT] = { RELAY_PID, RELAY_PID, RELAY_PID };
enum RelayStates { RELAY_IS_OFF, RELAY_IS_ON, SOMETHING_IS_BROKEN };
RelayStates relayStates[RELAY_COUNT] = { RELAY_IS_OFF, RELAY_IS_OFF, RELAY_IS_OFF };
RelayStates lastRelayStates[RELAY_COUNT] = {relayStates[0], relayStates[1], relayStates[2]};
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

// ---- Single int ----
void notifyClients(const char *key, int value) {
  char buffer[64];
  snprintf(buffer, sizeof(buffer), "{\"%s\":%d}", key, value);
#ifdef SINGLEPHASE_TESTMODE
  //delay(1000);
  Serial.printf("Sent int to client: %s\n", buffer);
#endif
  ws.textAll(buffer);
}

// ---- boolean ----
void notifyClients(const char *key, bool value) {
  char buffer[64];
  snprintf(buffer, sizeof(buffer), "{\"%s\":%s}", key, value ? "true" : "false");
#ifdef SINGLEPHASE_TESTMODE
  Serial.printf("Sent boolean to client: %s\n", buffer);
  //delay(1000);
#endif
  ws.textAll(buffer);
}

// ---- Single double ----
void notifyClients(const char *key, double value) {
  char buffer[64];
  snprintf(buffer, sizeof(buffer), "{\"%s\":%.2f}", key, value); // 3 decimals
#ifdef SINGLEPHASE_TESTMODE
  Serial.printf("Sent double to client: %s\n", buffer);
  //delay(1000);
#endif
  ws.textAll(buffer);
}

// ---- Single string ----
void notifyClients(const char *key, const char *value) {
  char buffer[128];
  snprintf(buffer, sizeof(buffer), "{\"%s\":\"%s\"}", key, value);
#ifdef SINGLEPHASE_TESTMODE
  Serial.printf("Sent string to client: %s\n", buffer);
  //delay(1000);
#endif
  ws.textAll(buffer);
}

// ---- RelayStates array ----
// ---- RelayModes array ----
template <typename T>
void notifyClients(const char *key, const T *arr) {
  char buffer[128];
  size_t pos = snprintf(buffer, sizeof(buffer), "{\"%s\":[", key);

  for (size_t i = 0; i < RELAY_COUNT; i++) {
    pos += snprintf(buffer + pos, sizeof(buffer) - pos,
                    (i < RELAY_COUNT - 1) ? "%d," : "%d", static_cast<int>(arr[i]));
  }

  snprintf(buffer + pos, sizeof(buffer) - pos, "]}");
  ws.textAll(buffer);
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
      notifyClients("enabled", true);
    } else if (msg == "disable") {
      enabled = false;
      notifyClients("enabled", false);
    } else if (msg.startsWith("target:")) {
      Setpoint = msg.substring(7).toFloat();
      notifyClients("target", Setpoint);
    } else if (msg.startsWith("relay:")) {
      int r = msg.substring(6,7).toInt() - 1; // relay index 0..2
      String mode = msg.substring(8);
      if (r >= 0 && r < 3) {
        if (mode == "on") relayModes[r] = RELAY_ON;
        else if (mode == "off") relayModes[r] = RELAY_OFF;
        else if (mode == "pid") relayModes[r] = RELAY_PID;
      }
      notifyClients("relayModes", relayModes);
    } else if (msg == "enabled") {
      client->text("enabled", enabled ? "true" : "false");
    } else if (msg = "ambiant") {
      client->text("ambiant", Ambiant);
    } else if (msg = "temp") {
      client->text("temp", Input);
    } else if (msg = "door") {
      client->text("door", door_is_open ? "open" : "closed");
    } else if (msg = "relays") {
      client->text("relayModes", relayModes);
      client->text("relayModes", relayStates);
    }
  }
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
             void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    Serial.printf("Client connected: #%u\n", client->id());
    //notifyClients("temp", Input);
    //notifyClients("ambiant", Ambiant);
  } else if (type == WS_EVT_DATA) {
    handleWebSocketMessage(arg, data, len);
  }
}

void setup() {
  EEPROM.begin(EEPROM_SIZE);

  pinMode(DOOR_SW, INPUT_PULLUP);
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

  // load saved parameters from EEPROM
  loadSetpoint();
  loadRelayModes();


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
  myPID.SetOutputLimits(0, 4); // 3 relays, but non-uniform power outputs!

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
    notifyClients("enabled", true);
  });
  
  server.on("/disable", HTTP_GET, [](AsyncWebServerRequest *request){
    enabled = false;
    request->send(200, "text/plain", "Sauna disabled");
    notifyClients("enabled", false);
  });
  
  server.on("/status.json", HTTP_GET, [](AsyncWebServerRequest *request){
    char msg[256];
    snprintf(msg, sizeof(msg),"{\n\
	\"ambiant\":%.2f,\n\
	\"temp\":%.2f,\n\
	\"target\":%.2f,\n\
	\"pid\":%.2f,\n\
	\"enabled\":%s,\n\
	\"door\":\"%s\",\n\
	\"relayModes\":[%d,%d,%d],\n\
	\"relayStates\":[%d,%d,%d],\n\
}",
	Ambiant, Input, Setpoint, Output,
  enabled ? "true" : "false", door_is_open ? "open" : "closed",
	relayModes[0], relayModes[1], relayModes[2],
	relayStates[0], relayStates[1], relayStates[2],
    request->send(200, "text/plain", msg);
#ifdef SINGLEPHASE_TESTMODE
    Serial.printf("sent status.json to client: %s\n", msg);
#endif
  });

  // simple GET handler: /set?temp=75
  server.on("/set", HTTP_GET, [](AsyncWebServerRequest *request){
    if (request->hasParam("temp")) {
      String val = request->getParam("temp")->value();
      double newTarget = val.toFloat();
      if (newTarget > 0 && newTarget < TEMP_ABSMAX) {
        Setpoint = newTarget;
	      notifyClients( "target", Setpoint);
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
    String msg = request->getParam("relay")->value(); // <-- declare msg here

    int r = msg.substring(6, 7).toInt() - 1;
    String mode = msg.substring(8);

    if (r >= 0 && r < (size_t)RELAY_COUNT) {
      if (mode == "on") relayModes[r] = RELAY_ON;
      else if (mode == "off") relayModes[r] = RELAY_OFF;
      else if (mode == "pid") relayModes[r] = RELAY_PID;
    }

    if (request->hasParam("save")) {
      EEPROM.put(ADDR_RELAYMODES, relayModes);
    }
    notifyClients("relayModes", relayModes);
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

  if (door_is_open != digitalRead(DOOR_SW)){
    door_is_open = digitalRead(DOOR_SW);
    notifyClients("door", door_is_open ? "open" : "closed");
  }

  if (enabled && !door_is_open) {
	digitalWrite(RELAY1, (relayModes[0] == RELAY_ON) ? RELAY_CLOSED :
		(relayModes[0] == RELAY_PID && Output >= 1 ? RELAY_CLOSED : RELAY_OPEN));
#ifndef SINGLEPHASE_TESTMODE
	digitalWrite(RELAY2, (relayModes[1] == RELAY_ON) ? RELAY_CLOSED :
		(relayModes[1] == RELAY_PID && Output >= 2 ? RELAY_CLOSED : RELAY_OPEN));
	digitalWrite(RELAY3, (relayModes[2] == RELAY_ON) ? RELAY_CLOSED :
		(relayModes[2] == RELAY_PID && Output >= 4 ? RELAY_CLOSED : RELAY_OPEN));
#endif
  } else {
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
  if (memcmp(relayStates, lastRelayStates, sizeof(relayStates)) != 0) {
#ifdef SINGLEPHASE_TESTMODE
    Serial.println("Relay state array changed");
#endif
    memcpy(lastRelayStates, relayStates, sizeof(relayStates));
    notifyClients("relayStates", relayStates);
  }

  if (millis() - lastSend > 10000) {
    notifyClients("temp", Input);
    notifyClients("ambiant", Ambiant);
    lastSend = millis();
  }

  delay(500);
}

