#pragma once
#include <Arduino.h>
#include <U8g2lib.h>

class DisplayManager {
public:
    DisplayManager() : _u8g2(U8G2_R0, U8X8_PIN_NONE) {}

    void begin() {
        delay(10);
        Wire.beginTransmission(0x3C);
        _available = (Wire.endTransmission() == 0);
        if (!_available) return;
        _u8g2.begin();
        _isOn = true;
        clear();
    }

    void clear() {
        _u8g2.clearBuffer();
        _u8g2.sendBuffer();
    }

    void on() {
        if (!_isOn) {
            _u8g2.setPowerSave(0);
            _isOn = true;
        }
    }

    void off() {
        if (_isOn) {
            _u8g2.setPowerSave(1);
            _isOn = false;
        }
    }

    bool isOn() { return _isOn; }
    bool isAvailable() { return _available; }

    void render(const char* status, float temp, float hum, bool tempValid, bool mqttOk, bool sensorAvailable = true, const char* ip = NULL) {
        if (!_available || !_isOn) return;
        _u8g2.clearBuffer();

        _u8g2.setDrawColor(1);
        _u8g2.drawBox(0, 0, 128, 12);
        _u8g2.setDrawColor(0);
        _u8g2.setFontMode(1);
        _u8g2.setFont(u8g2_font_profont10_tf);
        int w = _u8g2.getStrWidth(status);
        _u8g2.drawStr((128 - w) / 2, 10, status);
        _u8g2.setDrawColor(1);
        _u8g2.setFontMode(0);

        if (!sensorAvailable) {
            _u8g2.setFont(u8g2_font_profont17_tf);
            w = _u8g2.getStrWidth("Sensor Error");
            _u8g2.drawStr((128 - w) / 2, 35, "Sensor Error");
            w = _u8g2.getStrWidth("Check I2C conn");
            _u8g2.drawStr((128 - w) / 2, 55, "Check I2C conn");
        } else if (ip) {
            _u8g2.setFont(u8g2_font_logisoso28_tf);
            char numBuf[8];
            snprintf(numBuf, sizeof(numBuf), "%.1f", temp);
            int numW = _u8g2.getStrWidth(numBuf);
            _u8g2.setFont(u8g2_font_profont10_tf);
            int unitW = _u8g2.getStrWidth("\xC2\xB0" "C");
            int gap = 3;
            int totalW = numW + gap + unitW;
            int x = (128 - totalW) / 2;

            _u8g2.setFont(u8g2_font_logisoso28_tf);
            _u8g2.drawStr(x, 43, numBuf);
            _u8g2.setFont(u8g2_font_profont10_tf);
            _u8g2.drawUTF8(x + numW + gap, 43, "\xC2\xB0" "C");

            _u8g2.setFont(u8g2_font_profont10_tf);
            w = _u8g2.getStrWidth(ip);
            _u8g2.drawStr((128 - w) / 2, 60, ip);
        } else if (tempValid) {
            _u8g2.setFont(u8g2_font_logisoso28_tf);
            char numBuf[8];
            snprintf(numBuf, sizeof(numBuf), "%.1f", temp);
            int numW = _u8g2.getStrWidth(numBuf);
            _u8g2.setFont(u8g2_font_profont10_tf);
            int unitW = _u8g2.getStrWidth("\xC2\xB0" "C");
            int gap = 3;
            int totalW = numW + gap + unitW;
            int x = (128 - totalW) / 2;

            _u8g2.setFont(u8g2_font_logisoso28_tf);
            _u8g2.drawStr(x, 43, numBuf);
            _u8g2.setFont(u8g2_font_profont10_tf);
            _u8g2.drawUTF8(x + numW + gap, 43, "\xC2\xB0" "C");

            _u8g2.setFont(u8g2_font_profont17_tf);
            char buf[16];
            snprintf(buf, sizeof(buf), "Hum: %.0f%%", hum);
            w = _u8g2.getStrWidth(buf);
            _u8g2.drawStr((128 - w) / 2, 62, buf);
        }

        _u8g2.sendBuffer();
    }

private:
    U8G2_SSD1306_128X64_NONAME_F_HW_I2C _u8g2;
    bool _available = false;
    bool _isOn = false;
};
