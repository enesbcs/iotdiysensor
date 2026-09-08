# ESP8266-0.96Display-Temp-Sensor

ESP8266 (Wemos D1 Mini) based IoT temperature and humidity sensor with Home Assistant integration.
[EX05D9](https://bitekmindenhol.blog.hu/2018/07/14/wifi-s_homero_ex05d9)

> Built with [OpenCode](https://opencode.ai) CLI.

## Hardware

| GPIO | Function | Device |
|------|----------|--------|
| GPIO0 (D3) | Digital input (INPUT_PULLUP) | Push button – display on/off toggle |
| GPIO4 (D2) | I2C SDA | SSD1306 OLED display + SI7021 sensor |
| GPIO5 (D1) | I2C SCL | SSD1306 OLED display + SI7021 sensor |
| GPIO14 (D5) | Digital output (tone) | Passive piezo buzzer (RTTL melodies) |

### I2C devices

| Address | Device |
|---------|--------|
| 0x3C | SSD1306 128×64 OLED (U8g2 library) |
| 0x40 | Si7021 temperature/humidity sensor |

Board: **Wemos D1 Mini** (ESP8266EX @ 80 MHz, 4 MB Flash)

## Features

- **WiFi**: Captive portal based configuration in AP mode (172.218.28.1). Connects to a configured network in STA mode. 5 connection attempts, then AP fallback for 3 minutes before restart.
- **Display**: 3-line OLED (status in inverted bar, temperature with 1 decimal, humidity as integer). Auto-off after 2 minutes. Toggle via button or MQTT `/display/set`.
- **Sensor**: SI7021 polling every 20 seconds. Configurable temperature offset (-5..+5 °C, 0.5° steps).
- **MQTT**: Home Assistant autodiscovery (retained). Topics: `{deviceId}/temperature`, `{deviceId}/humidity`, `{deviceId}/display/set`, `{deviceId}/display/state`, `{deviceId}/rttl`.
- **Web UI**: Built-in web server at `/`. Main page: live sensor data and controls. `/settings`: WiFi, MQTT, offset configuration. `/hw`: GPIO map, I2C scan, Flash/Heap info. `/logs`: system log buffer (20 entries). `/update`: OTA firmware update.
- **Buzzer**: Play RTTL melodies via MQTT `/rttl` topic.
- **System logs**: 20-entry ring buffer. Access via Serial, `/api/logs` JSON, or `/logs` HTML page.

## Dependencies

| Library | License |
|---------|---------|
| [U8g2](https://github.com/olikraus/u8g2) (OLED) | BSD 2-Clause |
| [Adafruit Si7021 Library](https://github.com/adafruit/Adafruit_Si7021) | MIT |
| [PubSubClient](https://github.com/knolleary/pubsubclient) (MQTT) | MIT |
| [ArduinoJson](https://github.com/bblanchon/ArduinoJson) | MIT |

## Build & Upload

### Prerequisites

- [PlatformIO](https://platformio.org/) (Install: `pip install platformio`)
- ESP8266 packages (downloaded automatically by PlatformIO)

### Serial (first flash)

```bash
pio run -e d1_mini -t upload --upload-port /dev/ttyUSB0
```

### OTA (remote updates)

```bash
pio run -e d1_mini
curl -F "file=@.pio/build/d1_mini/firmware.bin" http://DEVICE_IP/update
```

Monitor:

```bash
pio device monitor -b 115200
```

## License

MIT – see the [LICENSE](LICENSE) file.
