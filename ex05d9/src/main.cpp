#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <DNSServer.h>
#include <Wire.h>
#include "ConfigManager.h"
#include "DisplayManager.h"
#include "SensorManager.h"
#include "MqttManager.h"
#include "BuzzerManager.h"
#include "config.h"

ConfigManager config;
DisplayManager display;
SensorManager sensor;
MqttManager mqtt;
BuzzerManager buzzer;

ESP8266WebServer server(80);
ESP8266HTTPUpdateServer httpUpdater;
DNSServer dns;

String deviceId;
bool apMode = false;
bool staConnected = false;
bool displayOn = true;
bool sensorValid = false;
bool mqttConnected = false;
float lastTemp = NAN;
float lastHum = NAN;

unsigned long lastSensorRead = 0;
unsigned long displayTimer = 0;
unsigned long lastMqttRetry = 0;
unsigned long lastButtonCheck = 0;
int lastButtonState = HIGH;
bool wasApBeforeSave = false;
unsigned long apFallbackStart = 0;
bool apHadVisitor = false;
unsigned long apLastActivity = 0;
unsigned long lastHeapLog = 0;
bool showIp = false;
unsigned long buttonPressStart = 0;

// Log buffer
static String logBuf[LOG_LINES];
static int logHead = 0;
static int logCount = 0;

static void addLog(const char* fmt, ...) {
    char buf[96];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    unsigned long ms = millis();
    char line[112];
    snprintf(line, sizeof(line), "[%lu.%03lu] %s", ms / 1000, ms % 1000, buf);
    logBuf[logHead] = String(line);
    logHead = (logHead + 1) % LOG_LINES;
    if (logCount < LOG_LINES) logCount++;
}

static void handleLogs();

static bool readSensor(float& temp, float& hum) {
    if (!sensor.read(temp, hum)) {
        addLog("Sensor read failed");
        return false;
    }
    temp += config.getTempOffset();
    return true;
}

static void buildStatusLine(char* buf, size_t len);
static void updateDisplay();
static void publishSensorData();
static void handleDisplayToggle();
static void handleReboot();
static void handleWifiConfig();
static void handleSettings();
static void handleTempOffset();
static void handleHwInfo();

static void mqttCallback(char* topic, byte* payload, unsigned int len) {
    String t = String(topic);
    String p;
    for (unsigned int i = 0; i < len; i++) p += (char)payload[i];

    if (t.endsWith("/display/set")) {
        if (p == "ON" || p == "1" || p == "true") {
            displayOn = true;
            display.on();
            displayTimer = millis();
            sensorValid = readSensor(lastTemp, lastHum);
            updateDisplay();
        } else if (p == "OFF" || p == "0" || p == "false") {
            displayOn = false;
            displayTimer = 0;
            display.off();
        }
        if (mqttConnected) {
            char t[64];
            snprintf(t, sizeof(t), "%s/display/state", deviceId.c_str());
            mqtt.publish(t, displayOn ? "ON" : "OFF", true);
        }
    } else if (t.endsWith("/rttl")) {
        buzzer.playRttl(p.c_str());
    }
}

static void buildStatusLine(char* buf, size_t len) {
    if (apMode) {
        snprintf_P(buf, len, PSTR("AP: %s"), deviceId.c_str());
    } else {
        String ssid = WiFi.SSID();
        int rssi = WiFi.RSSI();
        char mq = mqttConnected ? 'O' : 'X';

        if (WiFi.status() == WL_CONNECTED) {
            snprintf_P(buf, len, PSTR("%s %d %c"), ssid.c_str(), rssi, mq);
        } else {
            snprintf_P(buf, len, PSTR("WiFi disconnected"));
        }
    }
}

static void updateDisplay() {
    char status[30];
    buildStatusLine(status, sizeof(status));
    if (showIp) {
        char ip[16];
        snprintf(ip, sizeof(ip), "%s", WiFi.localIP().toString().c_str());
        display.render(status, lastTemp, lastHum, sensorValid, mqttConnected, sensor.isAvailable(), ip);
    } else {
        display.render(status, lastTemp, lastHum, sensorValid, mqttConnected, sensor.isAvailable());
    }
}

static void publishSensorData() {
    if (!mqttConnected || !sensorValid) return;
    char topic[64];
    char val[16];
    snprintf(topic, sizeof(topic), "%s/temperature", deviceId.c_str());
    snprintf(val, sizeof(val), "%.1f", lastTemp);
    mqtt.publish(topic, val);
    snprintf(topic, sizeof(topic), "%s/humidity", deviceId.c_str());
    snprintf(val, sizeof(val), "%.0f", lastHum);
    mqtt.publish(topic, val);
    snprintf(topic, sizeof(topic), "%s/display/state", deviceId.c_str());
    mqtt.publish(topic, displayOn ? "ON" : "OFF", true);
}

static void handleRoot() {
    if (apMode) {
        apHadVisitor = true;
        apLastActivity = millis();
        String html = F(
            "<!DOCTYPE html><html><head><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
            "<title>"); html += FW_NAME; html += F(" Config</title>"
            "<style>*{box-sizing:border-box;margin:0;padding:0}"
            "body{font-family:Arial,sans-serif;background:#f5f5f5;padding:20px}"
            "h1{font-size:1.3em;margin-bottom:15px;color:#333}"
            "form{max-width:360px;margin:0 auto;background:#fff;padding:20px;border-radius:8px;box-shadow:0 2px 8px rgba(0,0,0,.1)}"
            "label{display:block;margin:10px 0 3px;color:#555;font-size:.9em}"
            "input{width:100%;padding:10px;border:1px solid #ddd;border-radius:4px;font-size:1em}"
            "button{width:100%;padding:12px;margin-top:20px;background:#4CAF50;color:#fff;border:none;border-radius:4px;font-size:1em;cursor:pointer}"
            "button:hover{background:#45a049}"
            "</style></head><body>"
            "<form action=\"/save\" method=\"POST\">"
            "<h1>WiFi Configuration</h1>"
            "<label>SSID:</label><input type=\"text\" name=\"ssid\" required>"
            "<label>Password:</label><input type=\"password\" name=\"pass\">"
            "<button type=\"submit\">Save & Connect</button>"
            "</form></body></html>"
        );
        server.send(200, "text/html", html);
    } else {
        String html = F(
            "<!DOCTYPE html><html><head><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
            "<title>" FW_NAME "</title>"
            "<style>*{box-sizing:border-box;margin:0;padding:0}"
            "body{font-family:Arial,sans-serif;background:#f5f5f5;padding:20px}"
            ".card{max-width:360px;margin:0 auto 15px;background:#fff;padding:20px;border-radius:8px;box-shadow:0 2px 8px rgba(0,0,0,.1)}"
            "h1{font-size:1.3em;margin-bottom:10px;color:#333}"
            ".row{display:flex;justify-content:space-between;padding:6px 0;border-bottom:1px solid #eee;font-size:.9em}"
            ".row span{color:#888}.row b{color:#333}"
            "button{width:100%;padding:10px;margin-top:10px;background:#2196F3;color:#fff;border:none;border-radius:4px;font-size:.95em;cursor:pointer}"
            "button:hover{background:#1976D2}"
            ".on{color:#4CAF50;font-weight:bold}.off{color:#f44336;font-weight:bold}"
            ".btn_toggle{background:#FF9800}.btn_toggle:hover{background:#F57C00}"
            ".btn_reboot{background:#f44336}.btn_reboot:hover{background:#d32f2f}"
            ".btn_ota{background:#9C27B0}.btn_ota:hover{background:#7B1FA2}"
            "</style></head><body>"
            "<div class=\"card\">"
            "<h1>" FW_NAME " " FW_VERSION "</h1>"
            "<div id=\"data\"></div>"
            "<button class=\"btn_toggle\" onclick=\"toggleDisplay()\">Toggle Display</button>"
            "<button onclick=\"window.location.href='/settings'\">Settings</button>"
            "<button onclick=\"window.location.href='/hw'\">HW Info</button>"
            "<button onclick=\"window.location.href='/logs'\">System Logs</button>"
            "<button class=\"btn_ota\" onclick=\"window.location.href='/update'\">Firmware Update</button>"
            "<button class=\"btn_reboot\" onclick=\"reboot()\">Reboot</button>"
            "</div>"
            "<script>"
            "async function load(){try{"
            "let r=await fetch('/api/status'),d=await r.json();"
            "document.getElementById('data').innerHTML="
            "'<div class=\"row\"><span>Device</span><b>'+d.id+'</b></div>'"
            "+'<div class=\"row\"><span>WiFi</span><b>'+d.ssid+' ('+d.rssi+' dBm)</b></div>'"
            "+'<div class=\"row\"><span>Temperature</span><b>'+(d.temp!=null?d.temp+'&#176;C':'N/A')+'</b></div>'"
            "+'<div class=\"row\"><span>Humidity</span><b>'+(d.hum!=null?d.hum+'%':'N/A')+'</b></div>'"
            "+'<div class=\"row\"><span>MQTT</span><b class=\"'+(d.mqtt?'on':'off')+'\">'+(d.mqtt?'Connected':'Disconnected')+'</b></div>'"
             "+'<div class=\"row\"><span>Display</span><b class=\"'+(d.disp?'on':'off')+'\">'+(d.disp?'ON':'OFF')+'</b></div>';"
            "}catch(e){}}"
            "async function toggleDisplay(){"
            "let r=await fetch('/api/display/toggle',{method:'POST'});"
            "let d=await r.json();load();"
            "}"
            "async function reboot(){"
            "if(confirm('Reboot device?')){"
            "await fetch('/api/reboot',{method:'POST'});"
            "}"
            "}"
            "load();setInterval(load,5000);"
            "</script>"
            "<div style=\"text-align:center;font-size:.75em;color:#aaa;margin-top:10px\">" FW_FOOTER "</div>"
            "</body></html>"
        );
        server.send(200, "text/html", html);
    }
}

static void handleSave() {
    if (!server.hasArg("ssid")) {
        server.send(400, "text/plain", "Missing SSID");
        return;
    }
    String ssid = server.arg("ssid");
    String pass = server.arg("pass");
    config.setWiFi(ssid, pass);
    wasApBeforeSave = apMode;

    String html = F(
        "<!DOCTYPE html><html><head><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>Connecting...</title>"
        "<style>body{font-family:Arial,sans-serif;background:#f5f5f5;padding:40px;text-align:center}"
        "h1{color:#333}.ip{font-size:1.5em;color:#4CAF50;margin:20px 0}"
        "</style></head><body>"
        "<h1>Connecting to WiFi...</h1>"
        "<p>Check the display for the assigned IP address.</p>"
        "<p>Then reconnect to your WiFi network and open that IP in your browser.</p>"
        "<div id=\"info\"></div>"
        "<script>"
        "async function check(){try{"
        "let r=await fetch('/api/status'),d=await r.json();"
        "if(d.sta_connected&&d.ip!='0.0.0.0'){"
        "document.getElementById('info').innerHTML='<div class=\"ip\">IP: '+d.ip+'</div><a href=\"http://'+d.ip+'/\">Open device</a>';"
        "clearInterval(timer);"
        "}"
        "}catch(e){}}"
        "let timer=setInterval(check,2000);"
        "</script></body></html>"
    );
    server.send(200, "text/html", html);

    if (apMode) {
        apHadVisitor = true;
        apLastActivity = millis();
        WiFi.mode(WIFI_AP_STA);
        WiFi.softAPConfig(IPAddress(172, 218, 28, 1), IPAddress(172, 218, 28, 1), IPAddress(255, 255, 255, 0));
        WiFi.begin(ssid.c_str(), pass.c_str());
    }
}

static void handleApiStatus() {
    String json;
    json.reserve(600);
    json = "{";
    json += "\"id\":\""; json += deviceId; json += "\",";
    json += "\"ssid\":\""; json += WiFi.SSID(); json += "\",";
    json += "\"saved_ssid\":\""; json += config.getSSID(); json += "\",";
    json += "\"saved_pass\":\""; json += config.getPassword(); json += "\",";
    json += "\"rssi\":"; json += String(WiFi.RSSI()); json += ",";
    json += "\"ip\":\""; json += WiFi.localIP().toString(); json += "\",";
    json += "\"sta_connected\":"; json += WiFi.status() == WL_CONNECTED ? "true" : "false"; json += ",";
    if (sensorValid) {
        json += "\"temp\":"; json += String(lastTemp, 1); json += ",";
        json += "\"hum\":"; json += String(lastHum, 0); json += ",";
    } else {
        json += "\"temp\":null,";
        json += "\"hum\":null,";
    }
    json += "\"mqtt\":"; json += mqttConnected ? "true" : "false"; json += ",";
    json += "\"disp\":"; json += displayOn ? "true" : "false"; json += ",";
    json += "\"disp_available\":"; json += display.isAvailable() ? "true" : "false"; json += ",";
    json += "\"sensor_available\":"; json += sensor.isAvailable() ? "true" : "false"; json += ",";
    json += "\"mqtt_server\":\""; json += config.getMqttServer(); json += "\",";
    json += "\"mqtt_port\":"; json += String(config.getMqttPort()); json += ",";
    json += "\"mqtt_user\":\""; json += config.getMqttUser(); json += "\",";
    json += "\"mqtt_pass\":\""; json += config.getMqttPass(); json += "\",";
    json += "\"free_heap\":"; json += String(ESP.getFreeHeap()); json += ",";
    json += "\"temp_offset\":"; json += String(config.getTempOffset(), 1); json += ",";
    json += "\"flash_total\":"; json += String(ESP.getFlashChipRealSize()); json += ",";
    json += "\"flash_used\":"; json += String(ESP.getSketchSize()); json += ",";
    json += "\"flash_free\":"; json += String(ESP.getFreeSketchSpace());
    json += "}";
    server.send(200, "application/json", json);
}

static void handleMqttConfig() {
    String server_ = server.arg("mqtt_server");
    int port = server.arg("mqtt_port").toInt();
    String user = server.arg("mqtt_user");
    String pass = server.arg("mqtt_pass");
    if (pass.length() == 0) pass = config.getMqttPass();
    if (port == 0) port = 1883;
    config.setMqtt(server_, port, user, pass);

    mqtt.disconnect();
    mqttConnected = false;
    mqtt.begin(deviceId, config.getMqttServer(), config.getMqttPort(),
               config.getMqttUser(), config.getMqttPass());
    mqtt.setCallback(mqttCallback);
    if (staConnected && server_.length() > 0) {
        mqttConnected = mqtt.connect();
        addLog("MQTT config reconnect: %s (state=%d)", mqttConnected ? "OK" : "FAIL", mqtt.state());
    }

    String json = "{\"status\":\"MQTT configuration saved\"}";
    server.send(200, "application/json", json);
}

static void handleDisplayToggle() {
    if (displayOn) {
        displayOn = false;
        displayTimer = 0;
        display.off();
    } else {
        displayOn = true;
        display.on();
        displayTimer = millis();
        sensorValid = readSensor(lastTemp, lastHum);
        updateDisplay();
    }
    if (mqttConnected) {
        char t[64];
        snprintf(t, sizeof(t), "%s/display/state", deviceId.c_str());
        mqtt.publish(t, displayOn ? "ON" : "OFF", true);
    }
    server.send(200, "application/json", "{\"disp\":" + String(displayOn ? "true" : "false") + "}");
}

static void handleSettings() {
    String html = F(
        "<!DOCTYPE html><html><head><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>Settings</title>"
        "<style>*{box-sizing:border-box;margin:0;padding:0}"
        "body{font-family:Arial,sans-serif;background:#f5f5f5;padding:20px}"
        ".card{max-width:360px;margin:0 auto 15px;background:#fff;padding:20px;border-radius:8px;box-shadow:0 2px 8px rgba(0,0,0,.1)}"
        "h1{font-size:1.3em;margin-bottom:10px;color:#333}"
        "h2{font-size:1.1em;margin-bottom:10px;color:#555;border-bottom:1px solid #eee;padding-bottom:5px}"
        "label{display:block;margin:8px 0 3px;color:#555;font-size:.85em}"
        "input{width:100%;padding:8px;border:1px solid #ddd;border-radius:4px;font-size:.9em}"
        "button{width:100%;padding:10px;margin-top:15px;background:#2196F3;color:#fff;border:none;border-radius:4px;font-size:.95em;cursor:pointer}"
        "button:hover{background:#1976D2}"
        ".btn_reboot{background:#f44336}.btn_reboot:hover{background:#d32f2f}"
        ".btn_ota{background:#9C27B0}.btn_ota:hover{background:#7B1FA2}"
        ".btn_back{background:#607D8B}.btn_back:hover{background:#455A64}"
        "</style></head><body>"
        "<div class=\"card\">"
        "<h1>Settings</h1>"

        "<h2>WiFi</h2>"
        "<form id=\"wifiForm\">"
        "<label>SSID:</label><input type=\"text\" name=\"ssid\" id=\"wifi_ssid\" required>"
        "<label>Password:</label>"
        "<div style=\"display:flex;gap:4px;align-items:center\">"
        "<input type=\"password\" name=\"pass\" id=\"wifi_pass\" style=\"flex:1\">"
        "<button type=\"button\" id=\"togglePassBtn\" onclick=\"showPw()\" style=\"width:auto;padding:6px 10px;margin:0;white-space:nowrap;background:#607D8B\">Show</button>"
        "</div>"
        "<button type=\"submit\">Save WiFi</button>"
        "</form>"

        "<h2>MQTT</h2>"
        "<form id=\"mqttForm\">"
        "<label>Server:</label><input type=\"text\" name=\"mqtt_server\" id=\"mqtt_server\">"
        "<label>Port:</label><input type=\"number\" name=\"mqtt_port\" id=\"mqtt_port\" value=\"1883\">"
        "<label>Username:</label><input type=\"text\" name=\"mqtt_user\" id=\"mqtt_user\">"
        "<label>Password:</label>"
        "<div style=\"display:flex;gap:4px;align-items:center\">"
        "<input type=\"password\" name=\"mqtt_pass\" id=\"mqtt_pass\" style=\"flex:1\">"
        "<button type=\"button\" id=\"toggleMqttPass\" onclick=\"showMqttPw()\" style=\"width:auto;padding:6px 10px;margin:0;white-space:nowrap;background:#607D8B\">Show</button>"
        "</div>"
        "<button type=\"submit\">Save MQTT</button>"
        "</form>"

        "<h2>Temperature Offset</h2>"
        "<form id=\"offsetForm\">"
        "<label>Offset (\u00B0C, -5 to +5, step 0.5):</label>"
        "<input type=\"number\" name=\"temp_offset\" id=\"temp_offset\" step=\"0.5\" min=\"-5\" max=\"5\">"
        "<button type=\"submit\">Save Offset</button>"
        "</form>"

        "<button class=\"btn_back\" onclick=\"window.location.href='/'\">Back</button>"
        "</div>"
        "<script>"
        "async function loadForm(){try{"
        "let r=await fetch('/api/status'),d=await r.json();"
        "document.getElementById('wifi_ssid').value=d.saved_ssid||'';"
        "document.getElementById('wifi_pass').value=d.saved_pass||'';"
        "document.getElementById('mqtt_server').value=d.mqtt_server||'';"
        "document.getElementById('mqtt_port').value=d.mqtt_port||1883;"
        "document.getElementById('mqtt_user').value=d.mqtt_user||'';"
        "document.getElementById('mqtt_pass').value=d.mqtt_pass||'';"
        "document.getElementById('temp_offset').value=d.temp_offset||0;"
        "}catch(e){}}"
        "function showPw(){let p=document.getElementById('wifi_pass');let b=document.getElementById('togglePassBtn');if(p.type=='password'){p.type='text';b.textContent='Hide'}else{p.type='password';b.textContent='Show'}}"
        "function showMqttPw(){let p=document.getElementById('mqtt_pass');let b=document.getElementById('toggleMqttPass');if(p.type=='password'){p.type='text';b.textContent='Hide'}else{p.type='password';b.textContent='Show'}}"
        "loadForm();"
        "document.getElementById('wifiForm').onsubmit=async function(e){"
        "e.preventDefault();let f=new FormData(this);"
        "let r=await fetch('/api/config/wifi',{method:'POST',body:new URLSearchParams(f)});"
        "let d=await r.json();alert(d.status);"
        "};"
        "document.getElementById('mqttForm').onsubmit=async function(e){"
        "e.preventDefault();let f=new FormData(this);"
        "let r=await fetch('/api/config/mqtt',{method:'POST',body:new URLSearchParams(f)});"
        "let d=await r.json();alert(d.status);"
        "};"
        "document.getElementById('offsetForm').onsubmit=async function(e){"
        "e.preventDefault();let f=new FormData(this);"
        "let r=await fetch('/api/config/temp_offset',{method:'POST',body:new URLSearchParams(f)});"
        "let d=await r.json();alert(d.status);"
        "};"
        "</script></body></html>"
    );
    server.send(200, "text/html", html);
}

static void handleLogs() {
    String html = F(
        "<!DOCTYPE html><html><head><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>System Logs</title>"
        "<style>*{box-sizing:border-box;margin:0;padding:0}"
        "body{font-family:Arial,sans-serif;background:#f5f5f5;padding:20px}"
        ".card{max-width:400px;margin:0 auto 15px;background:#fff;padding:20px;border-radius:8px;box-shadow:0 2px 8px rgba(0,0,0,.1)}"
        "h1{font-size:1.3em;margin-bottom:10px;color:#333}"
        ".log{font-family:monospace;font-size:.75em;color:#333;white-space:pre-wrap;word-break:break-all;line-height:1.4}"
        ".btn_back{width:100%;padding:10px;margin-top:15px;background:#607D8B;color:#fff;border:none;border-radius:4px;font-size:.95em;cursor:pointer;display:block;text-align:center;text-decoration:none}"
        ".btn_back:hover{background:#455A64}"
        ".btn_refresh{width:100%;padding:10px;margin-top:10px;background:#2196F3;color:#fff;border:none;border-radius:4px;font-size:.95em;cursor:pointer}"
        ".btn_refresh:hover{background:#1976D2}"
        "</style></head><body>"
        "<div class=\"card\">"
        "<h1>System Logs</h1>"
        "<div class=\"log\" id=\"logData\">Loading...</div>"
        "<button class=\"btn_refresh\" onclick=\"loadLogs()\">Refresh</button>"
        "<a class=\"btn_back\" href=\"/\">Back</a>"
        "</div>"
        "<script>"
        "async function loadLogs(){try{"
        "let r=await fetch('/api/logs'),d=await r.json();"
        "document.getElementById('logData').innerText=(d.logs||[]).join('\\n')||'(empty)';"
        "}catch(e){document.getElementById('logData').innerText='Error loading logs';}}"
        "loadLogs();setInterval(loadLogs,10000);"
        "</script></body></html>"
    );
    server.send(200, "text/html", html);
}

static void handleHwInfo() {
    String html = F(
        "<!DOCTYPE html><html><head><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>HW Info</title>"
        "<style>*{box-sizing:border-box;margin:0;padding:0}"
        "body{font-family:Arial,sans-serif;background:#f5f5f5;padding:20px}"
        ".card{max-width:360px;margin:0 auto 15px;background:#fff;padding:20px;border-radius:8px;box-shadow:0 2px 8px rgba(0,0,0,.1)}"
        "h1{font-size:1.3em;margin-bottom:10px;color:#333}"
        "h2{font-size:1.1em;margin-bottom:10px;color:#555;border-bottom:1px solid #eee;padding-bottom:5px}"
        ".row{display:flex;justify-content:space-between;padding:6px 0;border-bottom:1px solid #eee;font-size:.9em}"
        ".row span{color:#888}.row b{color:#333}"
        ".on{color:#4CAF50;font-weight:bold}.off{color:#f44336;font-weight:bold}"
        ".btn_back{width:100%;padding:10px;margin-top:15px;background:#607D8B;color:#fff;border:none;border-radius:4px;font-size:.95em;cursor:pointer;display:block;text-align:center;text-decoration:none}"
        ".btn_back:hover{background:#455A64}"
        "</style></head><body>"
        "<div class=\"card\">"
        "<h1>HW Info</h1>"

        "<h2>GPIO Map</h2>"
        "<div class=\"row\"><span>GPIO0 (D3)</span><b>Button (INPUT_PULLUP)</b></div>"
        "<div class=\"row\"><span>GPIO4 (D2)</span><b>I2C SDA</b></div>"
        "<div class=\"row\"><span>GPIO5 (D1)</span><b>I2C SCL</b></div>"
        "<div class=\"row\"><span>GPIO14 (D5)</span><b>Piezo Buzzer</b></div>"

        "<h2>I2C Devices</h2>"
    );

    for (int addr = 1; addr < 128; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            String name;
            if (addr == 0x3C) name = "SSD1306 OLED";
            else if (addr == 0x40) name = "SI7021 Sensor";
            else name = "Unknown";
            html += "<div class=\"row\"><span>0x" + String(addr, HEX) + "</span><b class=\"on\">" + name + "</b></div>";
        }
    }

    html += F("<h2>Status</h2>");
    html += "<div class=\"row\"><span>Display</span><b>" + String(display.isAvailable() ? "OK" : "Error") + "</b></div>";
    html += "<div class=\"row\"><span>Sensor</span><b>" + String(sensor.isAvailable() ? "OK" : "Error") + "</b></div>";
    html += "<div class=\"row\"><span>Flash</span><b>" + String(ESP.getFlashChipRealSize() / 1024) + " KB</b></div>";
    html += "<div class=\"row\"><span>Chip ID</span><b>0x" + String(ESP.getChipId(), HEX) + "</b></div>";
    html += "<div class=\"row\"><span>Free Heap</span><b>" + String(ESP.getFreeHeap()) + " B</b></div>";
    html += "<div class=\"row\"><span>Firmware Used</span><b>" + String(ESP.getSketchSize() / 1024) + " KB</b></div>";
    html += "<div class=\"row\"><span>OTA Free</span><b>" + String(ESP.getFreeSketchSpace() / 1024) + " KB</b></div>";
    html += "<div class=\"row\"><span>MCU</span><b>ESP8266EX @ 80 MHz</b></div>";
    html += "<div class=\"row\"><span>SDK</span><b>" + String(ESP.getSdkVersion()) + "</b></div>";
    html += "<div class=\"row\"><span>Core</span><b>" + String(ESP.getCoreVersion()) + "</b></div>";
    html += "<div class=\"row\"><span>Firmware max</span><b>1020 KB</b></div>";
    html += F("<a class=\"btn_back\" href=\"/\">Back</a></div></body></html>");
    server.send(200, "text/html", html);
}

static void handleTempOffset() {
    float offset = server.arg("temp_offset").toFloat();
    if (offset < -5.0f || offset > 5.0f) {
        server.send(400, "application/json", "{\"status\":\"Offset must be between -5 and +5\"}");
        return;
    }
    config.setTempOffset(offset);
    server.send(200, "application/json", "{\"status\":\"Offset saved\"}");
}

static void handleReboot() {
    server.send(200, "application/json", "{\"status\":\"rebooting\"}");
    delay(500);
    ESP.restart();
}

static void handleWifiConfig() {
    String ssid = server.arg("ssid");
    String pass = server.arg("pass");
    if (ssid.length() == 0) {
        server.send(400, "application/json", "{\"status\":\"SSID required\"}");
        return;
    }
    config.setWiFi(ssid, pass);
    server.send(200, "application/json", "{\"status\":\"WiFi saved, reconnecting...\"}");
    delay(500);
    WiFi.disconnect();
    WiFi.begin(ssid.c_str(), pass.c_str());
}

static void handleNotFound() {
    if (apMode) {
        apHadVisitor = true;
        apLastActivity = millis();
        server.sendHeader("Location", "http://172.218.28.1/", true);
        server.send(302, "text/plain", "");
    } else {
        server.send(404, "text/plain", "Not found");
    }
}

static void checkButton() {
    unsigned long now = millis();
    if (now - lastButtonCheck < BUTTON_DEBOUNCE) return;
    lastButtonCheck = now;

    int reading = digitalRead(PIN_BUTTON);

    if (reading == LOW && lastButtonState == HIGH) {
        buttonPressStart = now;
    }

    if (reading == LOW && !showIp && displayOn && now - buttonPressStart >= 2000) {
        showIp = true;
        updateDisplay();
    }

    if (reading == HIGH && lastButtonState == LOW) {
        unsigned long pressMs = now - buttonPressStart;
        if (pressMs < 2000) {
            if (displayOn) {
                displayOn = false;
                displayTimer = 0;
                display.off();
            } else {
                displayOn = true;
                display.on();
                displayTimer = millis();
                sensorValid = readSensor(lastTemp, lastHum);
                showIp = false;
                updateDisplay();
            }
            if (mqttConnected) {
                char t[64];
                snprintf(t, sizeof(t), "%s/display/state", deviceId.c_str());
                mqtt.publish(t, displayOn ? "ON" : "OFF", true);
            }
        }
    }

    lastButtonState = reading;
}

void setup() {
    Serial.begin(115200);
    Serial.println();

    Wire.begin(I2C_SDA, I2C_SCL);

    config.begin();
    deviceId = config.getDeviceId();

    display.begin();
    sensor.begin();
    buzzer.begin(PIN_BUZZER);

    pinMode(PIN_BUTTON, INPUT_PULLUP);

    Serial.print("Device: ");
    Serial.println(deviceId);
    addLog("Device: %s", deviceId.c_str());

    if (display.isAvailable()) {
        display.on();
        displayOn = true;
        displayTimer = millis();
    }

    if (config.isWiFiConfigured()) {
        if (display.isAvailable()) {
            char status[30];
            snprintf(status, sizeof(status), "Connecting...");
            display.render(status, NAN, NAN, false, false, sensor.isAvailable());
        }
        Serial.println("Connecting to WiFi...");
        addLog("Connecting to WiFi");
        WiFi.mode(WIFI_STA);

        int attempt = 0;
        while (attempt < MAX_WIFI_ATTEMPTS && WiFi.status() != WL_CONNECTED) {
            if (attempt > 0) {
                WiFi.disconnect();
                delay(200);
            }
            attempt++;
            if (display.isAvailable()) {
                char status[30];
                snprintf(status, sizeof(status), "WiFi %d/%d", attempt, MAX_WIFI_ATTEMPTS);
                display.render(status, NAN, NAN, false, false, sensor.isAvailable());
            }
            WiFi.begin(config.getSSID().c_str(), config.getPassword().c_str());
            unsigned long start = millis();
            while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_ATTEMPT_TIMEOUT) {
                delay(100);
            }
            Serial.printf("WiFi attempt %d/%d\n", attempt, MAX_WIFI_ATTEMPTS);
            addLog("WiFi attempt %d/%d", attempt, MAX_WIFI_ATTEMPTS);
        }

        if (WiFi.status() == WL_CONNECTED) {
            staConnected = true;
            apMode = false;
            Serial.print("Connected, IP: ");
            Serial.println(WiFi.localIP());
            addLog("WiFi connected, IP: %s", WiFi.localIP().toString().c_str());
            WiFi.setSleepMode(WIFI_LIGHT_SLEEP);
            if (display.isAvailable()) {
                char status[30];
                snprintf(status, sizeof(status), "Connected!");
                display.render(status, NAN, NAN, false, false, sensor.isAvailable());
                delay(1000);
            }
        } else {
            Serial.println("All WiFi attempts failed, AP fallback for 3 min");
            addLog("WiFi failed, AP fallback");
        }
    }

    if (!staConnected) {
        apMode = true;
        apFallbackStart = millis();
        apHadVisitor = false;
        IPAddress apIP(172, 218, 28, 1);
        IPAddress netmask(255, 255, 255, 0);
        WiFi.mode(WIFI_AP);
        WiFi.softAPConfig(apIP, apIP, netmask);
        WiFi.softAP(deviceId.c_str());
        Serial.print("AP started, SSID: ");
        Serial.println(deviceId);
        Serial.print("AP IP: ");
        Serial.println(apIP);
        addLog("AP mode, SSID: %s", deviceId.c_str());

        dns.setErrorReplyCode(DNSReplyCode::NoError);
        dns.start(53, "*", apIP);

        if (display.isAvailable()) {
            char status[30];
            snprintf(status, sizeof(status), "AP: %s", deviceId.c_str());
            display.render(status, NAN, NAN, false, false, sensor.isAvailable());
        }
    }

    httpUpdater.setup(&server, "/update");

    server.on("/", handleRoot);
    server.on("/save", HTTP_POST, handleSave);
    server.on("/settings", handleSettings);
    server.on("/hw", handleHwInfo);
    server.on("/logs", handleLogs);
    server.on("/api/logs", []() {
        String json = "{\"logs\":[";
        int start = (logHead - logCount + LOG_LINES) % LOG_LINES;
        for (int i = 0; i < logCount; i++) {
            if (i > 0) json += ",";
            int idx = (start + i) % LOG_LINES;
            json += "\"" + logBuf[idx] + "\"";
        }
        json += "]}";
        server.send(200, "application/json", json);
    });
    server.on("/api/status", handleApiStatus);
    server.on("/api/config/mqtt", HTTP_POST, handleMqttConfig);
    server.on("/api/config/wifi", HTTP_POST, handleWifiConfig);
    server.on("/api/config/temp_offset", HTTP_POST, handleTempOffset);
    server.on("/api/display/toggle", HTTP_POST, handleDisplayToggle);
    server.on("/api/reboot", HTTP_POST, handleReboot);
    server.onNotFound(handleNotFound);
    server.begin();

    if (staConnected && config.isMqttConfigured() && config.getMqttServer().length() > 0) {
        mqtt.begin(deviceId, config.getMqttServer(), config.getMqttPort(),
                   config.getMqttUser(), config.getMqttPass());
        mqtt.setCallback(mqttCallback);
        mqttConnected = mqtt.connect();
        addLog("MQTT connect: %s (state=%d)", mqttConnected ? "OK" : "FAIL", mqtt.state());
    }
    if (mqttConnected) {
        char t[64];
        snprintf(t, sizeof(t), "%s/status", deviceId.c_str());
        mqtt.publish(t, "online", true);
    }

    lastSensorRead = millis() - SENSOR_INTERVAL + 2000;
    lastMqttRetry = millis();

    sensorValid = readSensor(lastTemp, lastHum);
    updateDisplay();
    publishSensorData();

    Serial.println("Setup complete");
    addLog("Setup complete");
}

void loop() {
    server.handleClient();

    if (apMode) {
        dns.processNextRequest();
    }

    bool wifiOk = (WiFi.status() == WL_CONNECTED);

    if (staConnected && !wifiOk) {
        Serial.println("WiFi connection lost");
        addLog("WiFi connection lost");
        staConnected = false;
        if (mqttConnected) {
            mqttConnected = false;
            mqtt.disconnect();
        }
        if (!apMode) {
            apMode = true;
            apFallbackStart = millis();
            apHadVisitor = false;
            WiFi.mode(WIFI_AP_STA);
            WiFi.softAPConfig(IPAddress(172, 218, 28, 1), IPAddress(172, 218, 28, 1), IPAddress(255, 255, 255, 0));
            WiFi.softAP(deviceId.c_str());
            dns.stop();
            dns.setErrorReplyCode(DNSReplyCode::NoError);
            dns.start(53, "*", IPAddress(172, 218, 28, 1));
            updateDisplay();
        }
    }

    if (!staConnected && wifiOk && config.isWiFiConfigured()) {
        Serial.println("WiFi connected");
        addLog("WiFi reconnected");
        staConnected = true;
        WiFi.setSleepMode(WIFI_LIGHT_SLEEP);
        apMode = false;
        WiFi.mode(WIFI_STA);
        if (config.isMqttConfigured() && config.getMqttServer().length() > 0) {
            mqtt.begin(deviceId, config.getMqttServer(), config.getMqttPort(),
                       config.getMqttUser(), config.getMqttPass());
            mqtt.setCallback(mqttCallback);
            mqttConnected = mqtt.connect();
            addLog("MQTT reconnect: %s (state=%d)", mqttConnected ? "OK" : "FAIL", mqtt.state());
            if (mqttConnected) {
                char t[64];
                snprintf(t, sizeof(t), "%s/status", deviceId.c_str());
                mqtt.publish(t, "online", true);
            }
        }
        sensorValid = readSensor(lastTemp, lastHum);
        updateDisplay();
        publishSensorData();
    }

    if (mqttConnected) {
        mqtt.loop();
        mqttConnected = mqtt.isConnected();
    }

    checkButton();

    unsigned long now = millis();

    if (now - lastSensorRead >= SENSOR_INTERVAL) {
        lastSensorRead = now;
        sensorValid = readSensor(lastTemp, lastHum);
        showIp = false;
        updateDisplay();
        publishSensorData();
    }

    if (now - lastHeapLog >= 3600000) {
        lastHeapLog = now;
        addLog("Free heap: %u B", ESP.getFreeHeap());
    }

    if (staConnected && config.isMqttConfigured() && config.getMqttServer().length() > 0
        && !mqttConnected && now - lastMqttRetry >= MQTT_RETRY_INTERVAL) {
        lastMqttRetry = now;
        if (WiFi.status() == WL_CONNECTED) {
            addLog("MQTT reconnect attempt");
            mqtt.begin(deviceId, config.getMqttServer(), config.getMqttPort(),
                       config.getMqttUser(), config.getMqttPass());
            mqtt.setCallback(mqttCallback);
            mqttConnected = mqtt.connect();
            addLog("MQTT reconnect: %s (state=%d)", mqttConnected ? "OK" : "FAIL", mqtt.state());
            if (mqttConnected) {
                char t[64];
                snprintf(t, sizeof(t), "%s/status", deviceId.c_str());
                mqtt.publish(t, "online", true);
                sensorValid = readSensor(lastTemp, lastHum);
                publishSensorData();
                updateDisplay();
            }
        }
    }

    if (apMode && !staConnected) {
        unsigned long apElapsed = millis() - apFallbackStart;
        if (apHadVisitor) {
            apElapsed = millis() - apLastActivity;
        }
        if (apElapsed >= AP_FALLBACK_TIMEOUT) {
            Serial.println("AP timeout - restarting");
            delay(500);
            ESP.restart();
        }
    }

    if (displayOn && display.isAvailable() && now - displayTimer >= DISPLAY_TIMEOUT) {
        displayOn = false;
        display.off();
        if (mqttConnected) {
            char t[64];
            snprintf(t, sizeof(t), "%s/display/state", deviceId.c_str());
            mqtt.publish(t, "OFF", true);
        }
    }

    buzzer.loop();
    delay(10);
}
