#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266mDNS.h>
#include <MQTT.h>

#define WIFI_SSID "MyHomeWiFi"
#define WIFI_PASSWORD "MyStrongPassword"
#define MQTT_HOST "example.mqtt.broker.com"
#define MQTT_PORT 8883
#define MQTT_USER "esp32_sensor"
#define MQTT_PASS "super-secret-password"
#define MOTOR_STATUS "motor/status"
#define OTA_USERNAME "admin1"
#define OTA_PASSWORD "ota-admin-pass"

const int RELAY_PIN = 2;
const int SWITCH_PIN = 3;
const char* hostname = "watermotor";

volatile bool switchState = false;
bool motorState = false;

WiFiClientSecure net;
ESP8266HTTPUpdateServer httpUpdater;
MQTTClient client;
ESP8266WebServer server(80);

unsigned long lastReconnectAttempt = 0;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;
int lastPhysicalReading = LOW;

void IRAM_ATTR handleSwitchInterrupt() {
  switchState = true;
}

void handleRoot() {
  httpUpdater.setup(
    &server, "/update",
    OTA_USERNAME,
    OTA_PASSWORD);
  server.begin();
}

void setMotor(bool turnOn, bool publishToMqtt = true) {
  motorState = turnOn;
  digitalWrite(RELAY_PIN, motorState ? HIGH : LOW);

  if (publishToMqtt && client.connected()) {
    client.publish(MOTOR_STATUS, motorState ? "true" : "false", true, 1);
  }
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
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(SWITCH_PIN, INPUT);

  lastPhysicalReading = digitalRead(SWITCH_PIN);
  setMotor(lastPhysicalReading == HIGH, false);

  attachInterrupt(digitalPinToInterrupt(SWITCH_PIN), handleSwitchInterrupt, CHANGE);

  // Insecure mode allows TLS connection without validating CA certificates
  net.setInsecure();
  WiFi.mode(WIFI_STA);
  WiFi.hostname(hostname);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

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