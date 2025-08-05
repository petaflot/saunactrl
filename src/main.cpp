#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <PID_v1.h>
#include <LittleFS.h>

#define ONE_WIRE_BUS D1      // GPIO for temperature sensors
#define RELAY1 D5
#define RELAY2 D6
#define RELAY3 D7

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
DeviceAddress sensor1;

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

const char* ssid = "engrenage";
const char* password = "3n9r3na93";

// PID setup
double Setpoint = 105.0, Input = 0, Output = 0;
PID myPID(&Input, &Output, &Setpoint, 2, 5, 1, DIRECT);

// Relay control
bool enabled = true;
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
    String msg = String((char*)data);
    Serial.println("Received WebSocket message: " + msg);
    if (msg == "enable") enabled = true;
    else if (msg == "disable") enabled = false;
    else if (msg.startsWith("target:")) Setpoint = msg.substring(7).toFloat();
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
  Serial.begin(115200);
  delay(1000);

  pinMode(RELAY1, OUTPUT);
  pinMode(RELAY2, OUTPUT);
  pinMode(RELAY3, OUTPUT);
  digitalWrite(RELAY1, LOW);
  digitalWrite(RELAY2, LOW);
  digitalWrite(RELAY3, LOW);

  sensors.begin();
  if (!sensors.getAddress(sensor1, 0)) {
    Serial.println("No temperature sensor found");
  } else {
    Serial.print("Sensor 0 address: ");
    for (uint8_t i = 0; i < 8; i++) {
      Serial.printf("%02X", sensor1[i]);
    }
    Serial.println();
  }

  if (!LittleFS.begin()) {
    Serial.println("Failed to mount LittleFS");
    return;
  } else {
    Serial.println("Listing files in LittleFS:");
    Dir dir = LittleFS.openDir("/");
    while (dir.next()) {
      Serial.print("  ");
      Serial.print(dir.fileName());
      Serial.print(" (");
      Serial.print(dir.fileSize());
      Serial.println(" bytes)");
    }
  }
  if (!LittleFS.exists("/index.html")) {
    Serial.println("index.html not found in LittleFS!");
  } else {
    Serial.println("index.html found in LittleFS");
  }

  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  myPID.SetMode(AUTOMATIC);
  myPID.SetOutputLimits(0, 3); // 3 relays max

  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

  ws.onEvent(onEvent);
  server.addHandler(&ws);
  server.begin();
}

void loop() {
  sensors.requestTemperatures();
  Input = sensors.getTempC(sensor1);

  if (Input == DEVICE_DISCONNECTED_C) {
    Serial.println("Sensor disconnected!");
    Output = 0;
  } else if (enabled) {
    myPID.Compute();
    Serial.printf("Temp: %.2f °C, Target: %.2f °C, PID Output: %.2f\n", Input, Setpoint, Output);
  } else {
    Output = 0;
  }

  digitalWrite(RELAY1, enabled && Output >= 1 ? HIGH : LOW);
  digitalWrite(RELAY2, enabled && Output >= 2 ? HIGH : LOW);
  digitalWrite(RELAY3, enabled && Output >= 3 ? HIGH : LOW);

  if (millis() - lastSend > 10000) {
    notifyClients();
    lastSend = millis();
  }

  delay(1000);
}

