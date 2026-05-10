#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_Si7021.h>

class SensorManager {
public:
    SensorManager() {}

    void begin() {
        Wire.beginTransmission(0x40);
        if (Wire.endTransmission() != 0) {
            _available = false;
            return;
        }
        if (!_sensor.begin()) {
            _available = false;
            return;
        }
        _available = true;
    }

    bool read(float& temperature, float& humidity) {
        if (!_available) return false;
        temperature = _sensor.readTemperature();
        humidity = _sensor.readHumidity();
        if (isnan(temperature) || isnan(humidity)) return false;
        return true;
    }

    bool isAvailable() { return _available; }

private:
    Adafruit_Si7021 _sensor;
    bool _available = false;
};
