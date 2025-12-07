#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <Firebase_ESP_Client.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"
#include "constant.h"
#include <time.h>

extern SensorPacket currentSensorData;
extern bool isSensorDataNew;

class GatewayNetwork {
private:
    FirebaseData   fbdo;
    FirebaseAuth   auth;
    FirebaseConfig config;
    CommandPacket  cmd;

    static void onRecv(const uint8_t * mac, const uint8_t * incoming, int len) {
        if (len == sizeof(SensorPacket)) {
            memcpy(&currentSensorData, incoming, sizeof(SensorPacket));
            isSensorDataNew = true;

            char keyChar = (currentSensorData.keyPress == 0) ? '-' : currentSensorData.keyPress;
            Serial.printf(
                "[Recv]  CTRL=%s | W=%d%%(%d) | T=%d | KEY='%c'\n",
                currentSensorData.controlState ? "ON" : "OFF",
                currentSensorData.waterPercent,
                currentSensorData.waterRaw,
                currentSensorData.tiltState,
                keyChar
            );
        }
    }

public:
    void begin() {
        WiFi.mode(WIFI_AP_STA);
        WiFi.setSleep(false);
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

        Serial.print("[WiFi] Connecting");
        while (WiFi.status() != WL_CONNECTED) {
            Serial.print(".");
            delay(300);
        }
        Serial.println("\n[WiFi] Connected ✔");
        Serial.print("[WiFi] IP: "); Serial.println(WiFi.localIP());

        // NTP
        configTime(7 * 3600, 0, "pool.ntp.org");
        while (time(nullptr) < 1000000000) {
            Serial.print(".");
            delay(300);
        }
        Serial.println("\n[Time] Synced");

        // Firebase
        config.api_key = FIREBASE_API_KEY;
        config.database_url = FIREBASE_DATABASE_URL;
        config.token_status_callback = tokenStatusCallback;

        Firebase.signUp(&config, &auth, "", "");
        Firebase.begin(&config, &auth);
        Firebase.reconnectWiFi(true);

        // ESP-NOW
        if (esp_now_init() != ESP_OK) {
            Serial.println("ESP-NOW Init Failed!");
            return;
        }

        esp_now_register_recv_cb(onRecv);

        esp_now_peer_info_t peer = {};
        memcpy(peer.peer_addr, SENSOR_NODE_MAC, 6);
        peer.channel = 0;
        peer.encrypt = false;

        if (esp_now_add_peer(&peer) != ESP_OK) {
            Serial.println("[ESP-NOW] Add peer failed");
        } else {
            Serial.println("[Network] ESP-NOW Ready");
        }
    }

    // ตอนนี้ command เป็น control อย่างเดียวแล้ว
    void send(bool state) {
        cmd.active = state;
        esp_err_t err = esp_now_send(SENSOR_NODE_MAC, (uint8_t*)&cmd, sizeof(cmd));
        if (err != ESP_OK) {
            Serial.printf("[GW] ESP-NOW send error: %d\n", (int)err);
        }
    }

    FirebaseData* get() { return &fbdo; }
    bool ok() { return Firebase.ready(); }

    void log(const String& s) {
        if (ok()) Firebase.RTDB.pushString(&fbdo, PATH_ERROR_LOG, s);
    }
};
