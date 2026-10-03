#include <MQTT.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266mDNS.h>
#include <LittleFS.h>
#include "credentials.h"

const char* AP_SSID = "hello world";
const char* AP_PASSWORD = "hello world";
const char* hostname = "watermotor";

const int RELAY_PIN = 2;
const int SWITCH_PIN = 3;

WiFiClientSecure net;
ESP8266HTTPUpdateServer httpUpdater;
MQTTClient client;
ESP8266WebServer server(80);

unsigned long lastDebounceTime = 0;
unsigned long staConnectStart = 0;
unsigned long lastStaRetry = 0;
unsigned long lastReconnectAttempt = 0;

const unsigned long debounceDelay = 100;
const unsigned long WIFI_TIMEOUT = 15000;

int lastPhysicalReading = LOW;
volatile bool switchState = false;
bool motorState = false;
bool staConnected = false;

String saved_ssid = "";
String saved_pass = "";

  // INTERRUPT STARTS HERE //

void IRAM_ATTR handleSwitchInterrupt() {
  switchState = true;
}

  // MOTOR LOGIC STARTS HERE //

void setMotor(bool turnOn, bool publishToMqtt = true) {
  motorState = turnOn;
  digitalWrite(RELAY_PIN, motorState ? HIGH : LOW);

  if (publishToMqtt && client.connected()) {
    client.publish(MOTOR_STATUS, motorState ? "true" : "false", true, 1);
  }
}

void handleSwitchDebounce() {
  if (!switchState) return; // if switchState is false the it will come out of the function.


  switchState = false;
  unsigned long now = millis();
  if (now - lastDebounceTime > debounceDelay) {
    int currentReading = digitalRead(SWITCH_PIN);
    if (currentReading != lastPhysicalReading) {
      lastPhysicalReading = currentReading;
      lastDebounceTime = now;
      setMotor(currentReading == HIGH, true);
    }
  }
}

  // STATION MAINTAINANCE STARTS HERE //

void checkSTAConnection(){
  if (staConnected) return;
  else if (WiFi.status() == WL_CONNECTED) {
    staConnected = true;
    Serial.print("STA IP: ");
    Serial.println(WiFi.localIP());
    return;
  }
  else if (millis() - staConnectStart > WIFI_TIMEOUT) {
    Serial.println("STA failed — AP still available at 192.168.4.1");
  }
}

void maintainSTA() {
  if (staConnected && WiFi.status() != WL_CONNECTED) {
    staConnected = false;
    Serial.println("STA lost");
  }

  if (!staConnected && saved_ssid.length() > 0) {
    if (millis() - lastStaRetry > 30000) {  // Retry every 30s
      lastStaRetry = millis();
      Serial.println("Retrying STA...");
      WiFi.begin(saved_ssid, saved_pass);
      staConnectStart = millis();
    }
  }
}

  // MQTT STARTS HERE //

void maintainMQTT() {
  if (WiFi.status() != WL_CONNECTED) return;

  if (!client.connected()) {
    unsigned long now = millis();
    if (now - lastReconnectAttempt > 5000) {
      lastReconnectAttempt = now;
      if (connectMQTT()) {
        lastReconnectAttempt = 0;
      }
    }
  } else {
    client.loop();
  }
}

bool connectMQTT() {
  String clientId = "ESP8266_WaterTank_" + String(ESP.getChipId());
  if (client.connect(clientId.c_str(), MQTT_USER, MQTT_PASS)) {
    client.subscribe(MOTOR_STATUS);
    client.publish(MOTOR_STATUS, motorState ? "true" : "false", true, 1);
    return true;
  }
  return false;
}

void messageReceived(String &topic, String &payload) {
  if (topic == MOTOR_STATUS) {
    if (payload == "true") {
      setMotor(true, false);
    } else if (payload == "false") {
      setMotor(false, false);
    }
  }
}

  // HANDALING WIFI CREDENTIALS START HERE //

void savedWiFiCredentials(const String &ssid, const String &pass){
  File file = LittleFS.open("/wifi.json", "w");
  if(!file) return;

  String json = "{\"ssid\":\"" + ssid + "\",\"pass\":\"" + pass + "\"}";
  file.print(json);
  file.close();
}

bool loadWiFiCredentials(String &ssid, String &pass){
  if(!LittleFS.exists("/wifi.json")) return false;

  File file = LittleFS.open("/wifi.json", "r");
  if (!file) return false;
  String content = file.readString();
  file.close();

  int ssidStart = content.indexOf("\"ssid\":\"") + 8;
  int ssidEnd = content.indexOf("\"", ssidStart);
  int passStart = content.indexOf("\"pass\":\"") + 8;
  int passEnd = content.indexOf("\"", passStart);
  if (ssidStart < 8 || passStart < 8) return false;
  ssid = content.substring(ssidStart, ssidEnd);
  pass = content.substring(passStart, passEnd);
  return ssid.length() > 0;
}

  // WEBSERVER STARTS HERE //

void handleRoot() {
  server.on("/", HTTP_GET,  handleConfigPage);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/status", HTTP_GET, handleStatus);
  httpUpdater.setup(
    &server, "/update",
    OTA_USERNAME,
    OTA_PASSWORD);
  server.begin();
}

void handleConfigPage() {
  String html = R"rawliteral(
    <!DOCTYPE html>
    <html lang="en">
    <head>
      <meta charset="utf-8">
      <meta name="viewport" content="width=device-width, initial-scale=1">
      <title>WiFi Config</title>
      <style>
        body { background-color: #121212; color: #e0e0e0; font-family: sans-serif; display: flex; justify-content: center; align-items: center; height: 100vh; margin: 0; }
        .card { background: #1e1e1e; padding: 2rem; border-radius: 8px; box-shadow: 0 4px 12px rgba(0,0,0,0.5); width: 100%; max-width: 320px; }
        h2 { margin-top: 0; color: #fff; text-align: center; }
        label { display: block; margin-bottom: 0.5rem; font-size: 0.9rem; }
        input[type="text"], input[type="password"] { width: 100%; padding: 0.5rem; margin-bottom: 1rem; background: #2d2d2d; border: 1px solid #444; color: #fff; border-radius: 4px; box-sizing: border-box; }
        input[type="submit"] { width: 100%; padding: 0.75rem; background: #3b82f6; border: none; color: white; font-weight: bold; border-radius: 4px; cursor: pointer; }
        input[type="submit"]:hover { background: #2563eb; }
      </style>
    </head>
    <body>
      <div class="card">
        <h2>WiFi Config</h2>
        <form action="/save" method="POST">
          <label>SSID</label>
          <input type="text" name="ssid" maxlength="32">
          <label>Password</label>
          <input type="password" name="pass" maxlength="64">
          <input type="submit" value="Save & Restart">
        </form>
      </div>
    </body>
    </html>
  )rawliteral";
  server.send(200, "text/html", html);
}

void handleStatus() {
  String html = R"rawliteral(
    <!DOCTYPE html>
    <html lang="en">
    <head>
      <meta charset="utf-8">
      <meta name="viewport" content="width=device-width, initial-scale=1">
      <title>System Status</title>
      <style>
        body { background-color: #121212; color: #e0e0e0; font-family: sans-serif; display: flex; justify-content: center; align-items: center; height: 100vh; margin: 0; }
        .card { background: #1e1e1e; padding: 2rem; border-radius: 8px; box-shadow: 0 4px 12px rgba(0,0,0,0.5); width: 100%; max-width: 320px; }
        h2 { margin-top: 0; color: #fff; text-align: center; }
        .status-item { margin-bottom: 1rem; font-size: 0.95rem; }
        .status-item span { font-weight: bold; color: #fff; }
      </style>
    </head>
    <body>
      <div class="card">
        <h2>System Status</h2>
        <div class="status-item">Motor State: <span>)rawliteral";
  html += String(motorState ? "ON" : "OFF");
  html += R"rawliteral(</span></div>
        <div class="status-item">STA IP: <span>)rawliteral";
  html += String(WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "disconnected");
  html += R"rawliteral(</span></div>
        <div class="status-item">AP IP: <span>)rawliteral";
  html += WiFi.softAPIP().toString();
  html += R"rawliteral(</span></div>
      </div>
    </body>
    </html>
  )rawliteral";
  server.send(200, "text/html", html);
}

void handleSave() {
  if (!server.hasArg("ssid") || !server.hasArg("pass")) {
    server.send(400, "text/plain", "Missing fields");
    return;
  }

  String newSsid = server.arg("ssid");
  String newPass = server.arg("pass");

  savedWiFiCredentials(newSsid, newPass);
  server.send(200, "text/plain", "Saved. Restarting...");
  delay(1000);
  ESP.restart();
}

  // WIFI MODE STARTS HERE //

void startDualWiFi(const String &sta_ssid, const String &sta_pass) {
  // Start AP first to guarantee user access
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());  // Always 192.168.4.1

  // Attempt STA connection
  WiFi.hostname(hostname);
  WiFi.setAutoReconnect(true);
  WiFi.begin(sta_ssid, sta_pass);
}

void startAPOffly() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());
}

void setup() {
  delay(2000);
  Serial.begin(115200, SERIAL_8N1, SERIAL_TX_ONLY);
  Serial.println();
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(SWITCH_PIN, INPUT);

  if (!LittleFS.begin()) {
    Serial.println("LittleFS mount failed — formatting");
    LittleFS.format();
    LittleFS.begin();
  }

  lastPhysicalReading = digitalRead(SWITCH_PIN);
  setMotor(lastPhysicalReading == HIGH, false);
  attachInterrupt(digitalPinToInterrupt(SWITCH_PIN), handleSwitchInterrupt, CHANGE);

  if (loadWiFiCredentials(saved_ssid, saved_pass)) {
    startDualWiFi(saved_ssid, saved_pass);
    staConnectStart = millis();
  } else {
    startAPOffly();
  }

  // Insecure mode allows TLS connection without validating CA certificates
  net.setInsecure();
  client.begin(MQTT_HOST, MQTT_PORT, net);
  client.onMessage(messageReceived);
  handleRoot();

  if (MDNS.begin(hostname)) {
    Serial.println("mDNS started");
    Serial.println("Address: http://watermotor.local");
  } else {
    Serial.println("mDNS failed!");
  }

}

void loop() {
  server.handleClient();
  MDNS.update();

  handleSwitchDebounce();
  checkSTAConnection();
  maintainSTA();
  maintainMQTT();
}