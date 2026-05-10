#pragma once
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>

class MqttManager {
public:
    MqttManager() : _mqttClient(_wifiClient) {}

    void begin(const String& deviceId, const String& server, int port,
               const String& user, const String& pass) {
        _deviceId = deviceId;
        _server = server;
        _port = port;
        _user = user;
        _pass = pass;
        _mqttClient.setServer(_server.c_str(), port);
        _mqttClient.setBufferSize(512);
    }

    void setCallback(MQTT_CALLBACK_SIGNATURE) {
        _mqttClient.setCallback(callback);
    }

    bool connect() {
        if (_server.length() == 0) return false;
        if (_mqttClient.connected()) return true;

        String clientId = _deviceId + "-" + String(random(0xFFFF), HEX);
        bool result;
        if (_user.length() > 0) {
            result = _mqttClient.connect(clientId.c_str(), _user.c_str(), _pass.c_str());
        } else {
            result = _mqttClient.connect(clientId.c_str());
        }
        if (result) {
            _discoverySent = false;
            String dispTopic = _deviceId + "/display/set";
            String rttlTopic = _deviceId + "/rttl";
            _mqttClient.subscribe(dispTopic.c_str());
            _mqttClient.subscribe(rttlTopic.c_str());
            sendDiscovery();
        }
        return result;
    }

    void disconnect() {
        _mqttClient.disconnect();
    }

    bool loop() {
        return _mqttClient.loop();
    }

    bool isConnected() {
        return _mqttClient.connected();
    }

    bool publish(const char* topic, const char* payload, bool retain = false) {
        return _mqttClient.publish(topic, payload, retain);
    }

    void sendDiscovery() {
        if (_discoverySent) return;
        _discoverySent = true;

        String base = _deviceId;

        String cfgT = "homeassistant/sensor/" + base + "-temperature/config";
        String payT = "{\"name\":\"" + base + " Temperature\",\"state_topic\":\"" + base +
                      "/temperature\",\"unit_of_measurement\":\"\xC2\xB0"
                      "C\",\"device_class\":\"temperature\",\"unique_id\":\"" +
                      base + "_temp\",\"device\":{\"identifiers\":[\"" + base +
                      "\"],\"name\":\"" + base + "\"}}";
        _mqttClient.publish(cfgT.c_str(), payT.c_str(), true);

        String cfgH = "homeassistant/sensor/" + base + "-humidity/config";
        String payH = "{\"name\":\"" + base + " Humidity\",\"state_topic\":\"" + base +
                      "/humidity\",\"unit_of_measurement\":\"%\",\"device_class\":\"humidity\",\"unique_id\":\"" +
                      base + "_hum\",\"device\":{\"identifiers\":[\"" + base +
                      "\"],\"name\":\"" + base + "\"}}";
        _mqttClient.publish(cfgH.c_str(), payH.c_str(), true);

        String cfgD = "homeassistant/switch/" + base + "-display/config";
        String payD = "{\"name\":\"" + base + " Display\",\"command_topic\":\"" + base +
                      "/display/set\",\"state_topic\":\"" + base +
                      "/display/state\",\"payload_on\":\"ON\",\"payload_off\":\"OFF\",\"unique_id\":\"" +
                      base + "_disp\",\"device\":{\"identifiers\":[\"" + base +
                      "\"],\"name\":\"" + base + "\"}}";
        _mqttClient.publish(cfgD.c_str(), payD.c_str(), true);
    }

    int state() { return _mqttClient.state(); }
    String getDeviceId() { return _deviceId; }

private:
    WiFiClient _wifiClient;
    PubSubClient _mqttClient;
    String _deviceId;
    String _server;
    int _port = 1883;
    String _user;
    String _pass;
    bool _discoverySent = false;
};
