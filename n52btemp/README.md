# nRF52840 BTHome Temp Sensor

Ultra-low-power wireless temperature / humidity / pressure sensor for **nRF52840 (SuperMini, nice!nano 0.6.0-compatible)** boards. It publishes sensor data as a **BTHome v2 (unencrypted)** BLE broadcast and otherwise stays in a 180-second deep-sleep cycle, making it suitable for battery-powered outdoor operation with a single CR123A cell.

> Built with [OpenCode](https://opencode.ai) CLI.

## Hardware

| GPIO (P.x) | Arduino pin | Function | Device |
| --- | --- | --- | --- |
| P0.13 | 13 | Digital output, LOW = rail off | LDO13 3.3 V rail enable |
| P0.15 | 15 | Digital output, active-HIGH (LOW = off) | Red status LED |
| P0.17 | 17 | I2C SDA | BME280 |
| P0.20 | 20 | I2C SCL | BME280 |
| P1.00 | 32 | Digital output, active-HIGH = VIN on | BME280 power switch (power gate) |

> Arduino pin numbers equal the GPIO register numbers on this board (`g_ADigitalPinMap` identity map).

### I2C devices

| Address | Device |
| --- | --- |
| 0x76 | BME280 temperature / humidity / pressure sensor |

Board: **SuperMini (nice!nano 0.6.0 UF2-compatible)** — nRF52840 @ 64 MHz Cortex-M4F, 1 MB Flash, 256 KB RAM, BLE 5.0.

## Features

- **MCU**: nRF52840 (64 MHz Cortex-M4F, 1 MB Flash, 256 KB RAM, LFCLK 32.768 kHz), Adafruit nRF52 core + FreeRTOS.
- **BLE**: BTHome v2 (unencrypted) beacon, non-connectable / non-scannable legacy advertising, 500 ms interval, 3 s broadcast per cycle, TX at max power (+8 dBm). The device name is intentionally not broadcast.
- **Payload** (`D2 FC 40 …`): battery percent (0x01), temperature (0x02), humidity (0x03), pressure (0x04), VDD voltage (0x0C). Battery and voltage are always included; sensor objects only when the BME280 read succeeds. VDD is measured with the integrated SAADC on VDD (12-bit, 1/6 gain, 0.6 V internal reference).
- **Battery percent**: CR123A lookup curve (3.20 V → 100 % … 2.00 V → 0 %) with linear interpolation.
- **Power management**: 180 s deep sleep per cycle (System ON low-power via FreeRTOS tickless idle → `sd_app_evt_wait`, internal RTC1 wake-up, RAM retained). The 3.3 V rail is kept off, the sensor is power-gated, and TWIM/I2C is disabled while sleeping. Estimated average draw ≈ 5 µA.
- **Robustness** (hardened build):
  - Watchdog timer: 30 s active-time budget, automatically paused while the CPU sleeps (SLEEP = Pause), so the 180 s sleep is never disturbed.
  - Timeout-bounded I2C/TWIM waits in the `Wire` library — a stuck SDA/SCL line can no longer hang the MCU; the sensor is simply skipped.
  - BLE advertising start is retried and failures degrade gracefully to battery-only broadcasts.
- **Home Assistant**: BTHome BLE integration auto-discovers the device and entities (temperature, humidity, pressure, battery, voltage).

## Dependencies

| Library | License |
| --- | --- |
| [Adafruit BME280 Library](https://github.com/adafruit/Adafruit_BME280) (default 2.3.0) | MIT |
| [Adafruit Bluefruit nRF52 Libraries](https://github.com/adafruit/Adafruit_Bluefruit_nRF52) | MIT |
| [Adafruit BusIO](https://github.com/adafruit/Adafruit_BusIO) (dependency of BME280) | MIT |
| Adafruit nRF52 Arduino core (pulled in by PlatformIO, FreeRTOS-based) | BSD / MIT / LGPL |

## Build & Upload

### Prerequisites

- [PlatformIO](https://platformio.org/) (Install: `pip install platformio`)
- nRF52 toolchain and framework packages (downloaded automatically by PlatformIO)

### Build

```
pio run
```

Firmware: `.pio/build/supermini/firmware.uf2` (≈ 260 KiB app, 521 UF2 blocks, RAM 15.6 KB).

### Flash (UF2 bootloader)

1. Double-tap the **RST** button to enter the bootloader (mounts as `NICENANO`).
2. Copy `.pio/build/supermini/firmware.uf2` onto the `NICENANO` drive.
3. The board reboots automatically and starts broadcasting.

The `scripts/elf2uf2.py` build hook packs the firmware with the Adafruit nRF52 UF2 family ID (0xADA52840) expected by the nice!nano-compatible bootloader. The generated firmware has no serial output.

### Recommended framework tweaks

Two small patches to the installed Adafruit nRF52 core are assumed (they are not part of `src/` and are lost on `pio pkg update` / package reinstall):

- `scripts/wire_nrf52_timeout.patch` — adds a 50 ms deadline to the TWIM wait loops in `libraries/Wire/Wire_nRF52.cpp` (prevents infinite busy-loops on a stuck I2C bus). Apply from the framework root with `patch -p1 < scripts/wire_nrf52_timeout.patch`.
- `scripts/pca10056_variant_init.patch` — replaces the `initVariant()` body in `variants/pca10056/variant.cpp` to drive P0.13 (3.3 V rail) and P0.15 (red LED) LOW as early as possible. Apply from the framework root with `patch -p1 < scripts/pca10056_variant_init.patch`.

## License

MIT — see the [LICENSE](LICENSE) file.
