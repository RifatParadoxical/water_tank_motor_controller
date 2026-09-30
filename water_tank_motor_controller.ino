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

unsigned long lastReconnectAttempt = 0;
unsigned long lastDebounceTime = 0;
unsigned long staConnectStart = 0;

const unsigned long debounceDelay = 50;
const unsigned long WIFI_TIMEOUT = 15000;

int lastPhysicalReading = LOW;
volatile bool switchState = false;
bool motorState = false;
bool staConnected = false;

String saved_ssid = "";
String saved_pass = "";

void IRAM_ATTR handleSwitchInterrupt() {
  switchState = true;
}

void checkSTAConnection(){
  if (staConnected) return;

    if (WiFi.status() == WL_CONNECTED) {
      staConnected = true;
      Serial.print("STA IP: ");
      Serial.println(WiFi.localIP());
      return;
    }
    if (millis() - staConnectStart > WIFI_TIMEOUT) {
      Serial.println("STA failed — AP still available at 192.168.4.1");
    }
}

void savedWiFiCredentials(const String &ssid, const String &pass){
  File file = LittleFS.open("/wifi.json", "w");
  if(!file) return;

  String json = "{\"ssid\":\"" + ssid + "\",\"pass\":\"" + pass + "\"}";
  file.print(json);
  file.close()
}

bool loadWiFiCredentials(String &ssid, String &pass){
  if(!LittleFS.exists("/wifi.json")) return false;

  File file = LittleFS.open("/wifi.json", "r");
  if (!file) return false
  String content = file.readString();
  file.close();

  int ssidStart = content.indexOf("\"ssid\":\"") + 8;
  int ssidEnd = content.indexOf("\"", ssidStart);
  int passStart = context.indexOf("\"pass\":\"") + 8;
  int passEnd = context.indexOf("\"", passStart);
  if (ssidStart < 8 || passStart < 8) return false;
  ssid = content.substring(ssidStart, ssidEnd);
  pass = content.substring(passStart, passEnd);
  return ssid.length() > 0;
}

void handleRoot() {
  httpUpdater.setup(
    &server, "/update",
    OTA_USERNAME,
    OTA_PASSWORD);
  server.on("/", HTTP_GET,  handleConfigPage);
  server.on("/save", HTTP_POST, handleSave);
  server.begin();
}

void handleConfigPage() {
  String html = R"rawliteral(
    <!DOCTYPE html><html><body>
    <h2>WiFi Config</h2>
    <form action="/save" method="POST">
      SSID: <input name="ssid" length="32"><br>
      Password: <input name="pass" length="64"><br>
      <input type="submit" value="Save & Restart">
    </form>
    </body></html>
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

void setMotor(bool turnOn, bool publishToMqtt = true) {
  motorState = turnOn;
  digitalWrite(RELAY_PIN, motorState ? HIGH : LOW);

  if (publishToMqtt && client.connected()) {
    client.publish(MOTOR_STATUS, motorState ? "true" : "false", true, 1);
  }
}

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

void messageReceived(String &topic, String &payload) {
  if (topic == MOTOR_STATUS) {
    if (payload == "true") {
      setMotor(true, false);
    } else if (payload == "false") {
      setMotor(false, false);
    }
  }
}

bool connectMQTT() {
  if (client.connect("ESP32_WaterTank_Client", MQTT_USER, MQTT_PASS)) {
    client.subscribe(MOTOR_STATUS);
    client.publish(MOTOR_STATUS, motorState ? "true" : "false", true, 1);
    return true;
  }
  return false;
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

  // Insecure mode allows TLS connection without validating CA certificates
  net.setInsecure();
  startDualWiFi();

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  client.begin(MQTT_HOST, MQTT_PORT, net);
  client.onMessage(messageReceived);
  Serial.println(WiFi.localIP());
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
  if (switchState) {
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

  if (WiFi.status() == WL_CONNECTED) {
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
}