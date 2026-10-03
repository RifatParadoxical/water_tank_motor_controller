# ESP8266 Water Tank Motor Controller

Firmware for an ESP8266 ESP-01S that controls a water-pump motor through a relay, supports a local physical switch, and reports/accepts motor commands over MQTT.

> **Electrical safety:** Mains voltage and pump motors can cause fire, electric shock, or death. Use a correctly rated, enclosed relay/contactor and have mains wiring installed and checked by a qualified person. Never connect mains voltage to the ESP8266. Test the logic with a low-voltage load before connecting a pump.

## Features

- Starts a Wi-Fi setup access point so router credentials can be configured from a browser.
- Keeps the access point available while attempting to join the configured Wi-Fi network.
- Controls the relay from a physical input and from MQTT.
- Serves a JSON status endpoint and an HTTP firmware-update page.
- Saves Wi-Fi credentials in ESP8266 LittleFS.

## Hardware

- ESP8266 ESP-01S module
- Stable regulated 3.3 V supply for the ESP8266 (allow at least 500 mA peak current); do not power it from a 5 V pin
- Relay module or properly rated contactor interface compatible with 3.3 V GPIO logic
- Physical switch and external pull-down resistor for GPIO3
- Suitable enclosure, motor protection, and correctly rated power wiring

### Signal connections

| ESP-01S signal | Firmware pin | Connection / behavior |
| --- | --- | --- |
| GPIO2 | `RELAY_PIN` | Relay input; HIGH means motor ON, LOW means OFF |
| GPIO3 / RX | `SWITCH_PIN` | Switch input; HIGH means motor ON, LOW means OFF |
| VCC | 3.3 V | Regulated supply; share ground with the relay interface |
| GND | GND | Common ground for ESP8266 and low-voltage control circuitry |
| CH_PD / EN | — | Must be held HIGH for normal operation |
| RST | — | Keep HIGH; pull LOW only to reset |
| GPIO0 | — | Keep HIGH during normal boot; pull LOW only for programming mode |

The firmware configures GPIO3 as `INPUT` without an internal pull resistor. Wire the switch between 3.3 V and GPIO3, and add an external pull-down resistor from GPIO3 to GND so the input is LOW when the switch is open. The switch input is interpreted as a requested motor state, not as a momentary toggle: HIGH turns the motor on; LOW turns it off. GPIO3 is also the ESP-01S serial RX pin, so avoid a connected serial adapter driving it while the switch is in use.

GPIO2 is also a boot-strapping pin on the ESP8266. Ensure the relay interface does not hold it LOW during reset or prevent normal boot. Relay inputs, pump loads, and motor transients must not exceed the GPIO's voltage/current limits; use an appropriate driver/interface and suppression components as required by the relay and motor manufacturer.

## Software setup

1. Install Arduino IDE and the **ESP8266 Arduino core**.
2. Select an ESP8266 board compatible with the ESP-01S. This project is built for the generic ESP8266 target; select the matching board and flash settings for your installed core.
3. Install the **MQTT** library that provides `MQTT.h` (the 256dpi Arduino MQTT library).
4. Open `water_tank_motor_controller.ino` from this project folder. Keep `credentials.h` beside the sketch.
5. Set the broker host, port, MQTT username/password, and OTA username/password in `credentials.h`. Do not publish real credentials in source control or screenshots. The current credential file contains secrets; rotate any values that have been shared or committed, and keep a private local copy for building.
6. Connect the ESP-01S using a 3.3 V USB-to-serial adapter. Use the correct boot-mode wiring for programming; disconnect any adapter signal that drives GPIO3 during normal switch operation.
7. Compile and upload the sketch, then open the serial monitor at **115200 baud** to see startup and network messages.

The ESP8266 core supplies the Wi-Fi, web server, mDNS, HTTP update, and LittleFS components included by the sketch. No separate library installation is needed for those headers when the core is installed.

## First-time Wi-Fi setup

1. Power the controller. If no saved Wi-Fi credentials exist, it starts an access point named **hello world** with password **hello world**.
2. Connect a phone or computer to that network. The AP address is normally `192.168.4.1`.
3. Open [http://192.168.4.1](http://192.168.4.1) in a browser.
4. Enter the home/installation Wi-Fi SSID and password, then submit **Save & Restart**.
5. The controller saves these values in LittleFS and restarts. It keeps the setup AP available while attempting to connect to the router.
6. Check the serial monitor for the station IP. When connected to the same LAN, `http://watermotor.local` may also work if the client network supports mDNS.

The AP name and password are hard-coded in the sketch as `AP_SSID` and `AP_PASSWORD`; change them before deployment. The setup form uses plain HTTP: the router password is sent without transport encryption and stored in `/wifi.json` in LittleFS without encryption. Only provision the device on a trusted local network, and physically protect the controller and its flash storage. The status endpoint is also plain HTTP. The AP credentials are shared by every device running this firmware.

## Using the controller

### Physical switch

The firmware reads the GPIO3 level after a 100 ms debounce interval. A HIGH level energizes the relay output; a LOW level de-energizes it. Use a maintained switch or other circuit that provides a stable level. Confirm the relay's active level and the pump's safe state before connecting the motor.

### MQTT control

Configure a reachable MQTT broker in `credentials.h`. The controller connects using TLS to the configured host and port, and uses this topic:

| Topic | Direction | Payload | Effect |
| --- | --- | --- | --- |
| `motor/status` | Subscribe and publish | `true` or `false` | `true` turns the motor on; `false` turns it off |

On successful connection, the device subscribes to `motor/status` and publishes its current state as a retained QoS 1 message. A received `true` or `false` command changes the relay state. Other payloads are ignored. Physical switch changes publish the resulting state when MQTT is connected.

**TLS note:** the current firmware calls `net.setInsecure()`, which encrypts the connection but does not verify the broker's certificate. This leaves the connection vulnerable to an active impersonation attack. Add proper certificate validation before using this on an untrusted network.

### Web status and firmware update

From a device on the same network, open the controller's station IP or `http://watermotor.local`:

- `/` — Wi-Fi configuration form
- `/status` — JSON status, for example `{"motor":false,"sta":"192.168.1.50","ap":"192.168.4.1"}`
- `/update` — ESP8266 HTTP OTA update page; authenticate with `OTA_USERNAME` and `OTA_PASSWORD` from `credentials.h`

The update page is served over HTTP, not HTTPS. Use it only on a trusted network. Protect the OTA credentials and change the defaults before deployment.

## Startup and connection behavior

- With saved Wi-Fi credentials, the device starts in AP + station mode and attempts to connect to the router. The AP remains available for access.
- Without saved credentials, it starts in AP-only mode for provisioning.
- If the router is unavailable, the firmware retries the station connection periodically; MQTT is attempted only while station Wi-Fi is connected.
- MQTT reconnect attempts are spaced five seconds apart.
- The relay output is initialized from the GPIO3 switch level during startup.
- Wi-Fi credentials are stored as `/wifi.json` in LittleFS. Erasing or formatting the filesystem removes them and requires provisioning again.

## Credentials

```C
#ifndef CREDENTIALS_H
#define CREDENTIALS_H

#define MQTT_HOST "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx.s1.eu.hivemq.cloud"
#define MQTT_PORT 8883
#define MQTT_USER "water_controller"
#define MQTT_PASS "passformqtt"
#define MOTOR_STATUS "motor/status"
#define OTA_USERNAME "admin"
#define OTA_PASSWORD "passforota"

#endif
```

## Troubleshooting

| Symptom | Checks |
| --- | --- |
| Setup network is missing | Confirm the ESP-01S has a stable 3.3 V supply and has completed startup. Check that the phone/computer is not automatically switching away from the AP. |
| Cannot open `192.168.4.1` | Confirm you are connected to the controller AP and use `http://`, not `https://`. |
| Controller does not join router Wi-Fi | Re-enter the SSID/password, check 2.4 GHz Wi-Fi coverage and router compatibility, then read the serial monitor at 115200 baud. |
| MQTT does not connect | Verify the broker host, TLS port, username/password, outbound network access, and that the broker accepts the client. |
| Motor does not follow the switch | Verify the GPIO3 pull-down and switch wiring, common ground, and HIGH/LOW behavior; test with the motor disconnected. |
| ESP8266 fails to boot after relay wiring | Check GPIO2 boot-level requirements and confirm the relay module is not loading the pin during reset. |
| `watermotor.local` does not resolve | Use the station IP printed to Serial; mDNS is not supported by every client or network. |

## Security and deployment checklist

- Replace the hard-coded setup AP name/password and OTA credentials.
- Rotate any MQTT or OTA secret that has been exposed or committed, and do not commit a populated `credentials.h` to a public repository.
- Enable broker certificate validation instead of `setInsecure()` before relying on MQTT security.
- Keep the setup AP and OTA page off untrusted networks; both are plain HTTP.
- Verify relay behavior, boot behavior, loss-of-network behavior, and the pump's fail-safe state with the motor disconnected first.
- Use appropriate circuit protection, a correctly rated relay/contactor, an enclosure, and qualified mains installation.

## Project files

- `water_tank_motor_controller.ino` — ESP8266 firmware
- `credentials.h` — local MQTT and OTA settings; contains sensitive credentials
- `assest/ESP_01S_PINOUT.jpg` — ESP-01S pinout reference
- `assest/ESP01.pdf` — ESP-01 reference document
