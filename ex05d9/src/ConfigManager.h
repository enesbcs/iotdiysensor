#pragma once
#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

class ConfigManager {
public:
    ConfigManager() {}

    bool begin() {
        if (!LittleFS.begin()) return false;
        _deviceId = getDeviceId();
        return load();
    }

    String getDeviceId() {
        if (_deviceId.length() > 0) return _deviceId;
        uint8_t mac[6];
        WiFi.macAddress(mac);
        char id[16];
        snprintf(id, sizeof(id), "espiot-%02X%02X%02X", mac[3], mac[4], mac[5]);
        _deviceId = String(id);
        return _deviceId;
    }

    String getSSID() { return _ssid; }
    String getPassword() { return _pass; }
    String getMqttServer() { return _mqttServer; }
    int getMqttPort() { return _mqttPort; }
    String getMqttUser() { return _mqttUser; }
    String getMqttPass() { return _mqttPass; }
    float getTempOffset() { return _tempOffset; }

    void setWiFi(const String& ssid, const String& pass) {
        _ssid = ssid;
        _pass = pass;
        save();
    }

    void setMqtt(const String& server, int port, const String& user, const String& pass) {
        _mqttServer = server;
        _mqttPort = port;
        _mqttUser = user;
        _mqttPass = pass;
        save();
    }

    void setTempOffset(float offset) {
        _tempOffset = offset;
        save();
    }

    bool isWiFiConfigured() { return _ssid.length() > 0; }
    bool isMqttConfigured() { return _mqttServer.length() > 0; }

private:
    String _deviceId;
    String _ssid, _pass;
    String _mqttServer;
    int _mqttPort = 1883;
    String _mqttUser, _mqttPass;
    float _tempOffset = 0.0f;

    bool load() {
        if (!LittleFS.exists("/config.json")) return false;
        File f = LittleFS.open("/config.json", "r");
        if (!f) return false;
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, f);
        f.close();
        if (err) return false;
        _ssid = doc["ssid"] | "";
        _pass = doc["pass"] | "";
        _mqttServer = doc["mqtt_server"] | "";
        _mqttPort = doc["mqtt_port"] | 1883;
        _mqttUser = doc["mqtt_user"] | "";
        _mqttPass = doc["mqtt_pass"] | "";
        _tempOffset = doc["temp_offset"] | 0.0f;
        return true;
    }

    bool save() {
        File f = LittleFS.open("/config.json", "w");
        if (!f) return false;
        JsonDocument doc;
        doc["ssid"] = _ssid;
        doc["pass"] = _pass;
        doc["mqtt_server"] = _mqttServer;
        doc["mqtt_port"] = _mqttPort;
        doc["mqtt_user"] = _mqttUser;
        doc["mqtt_pass"] = _mqttPass;
        doc["temp_offset"] = _tempOffset;
        serializeJson(doc, f);
        f.close();
        return true;
    }
};
