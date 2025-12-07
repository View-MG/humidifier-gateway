#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <Firebase_ESP_Client.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"
#include "constant.h"
#include <time.h>

// ตัวแปร global เก็บค่าจาก Sensor Node (ตามที่ตกลงกับ Sensor Node)
extern SensorPacket currentSensorData;
extern bool isSensorDataNew;

class GatewayNetwork{
private:
    FirebaseData fbdo;
    FirebaseAuth auth;
    FirebaseConfig config;
    CommandPacket cmd;

    static void onRecv(const uint8_t * mac, const uint8_t *incoming, int len){
        if(len == sizeof(SensorPacket)){
            memcpy(&currentSensorData, incoming, sizeof(SensorPacket));
            isSensorDataNew = true;

            const SensorPacket &d = currentSensorData;
            const char *tiltTxt =
                (d.tiltState == TILT_FALL)    ? "FALL" :
                (d.tiltState == TILT_WARNING) ? "WARN" :
                                                "NORMAL";

            Serial.printf("[Recv]  CTRL=%s | W=%d%%(%d) | T=%s | KEY='%c'\n",
                          d.controlState ? "ON" : "OFF",
                          d.waterPercent,
                          d.waterRaw,
                          tiltTxt,
                          d.keyPress ? d.keyPress : '-');
        }
    }

public:
    void begin(){
        WiFi.mode(WIFI_AP_STA);
        WiFi.setSleep(false);
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

        Serial.print("[WiFi] Connecting");
        while(WiFi.status() != WL_CONNECTED){
            Serial.print(".");
            delay(300);
        }
        Serial.println("\n[WiFi] Connected ✔");

        // NTP
        configTime(7*3600, 0, "pool.ntp.org");
        Serial.print("[Time] Syncing");
        while(time(nullptr) < 1000000000){
            Serial.print(".");
            delay(300);
        }
        Serial.println("\n[Time] Synced");

        // Firebase
        config.api_key = FIREBASE_API_KEY;
        config.database_url = FIREBASE_DATABASE_URL;
        config.token_status_callback = tokenStatusCallback;

        if (!Firebase.signUp(&config, &auth, "", "")) {
            Serial.printf("[Firebase] SignUp Error: %s\n", config.signer.signupError.message.c_str());
        }
        Firebase.begin(&config, &auth);
        Firebase.reconnectWiFi(true);

        // ESP-NOW
        if(esp_now_init() != ESP_OK){
            Serial.println("[ESP-NOW] Init Failed!");
            return;
        }

        esp_now_register_recv_cb(onRecv);

        esp_now_peer_info_t peer = {};
        memcpy(peer.peer_addr, SENSOR_NODE_MAC, 6);
        peer.channel = 0;
        peer.encrypt = false;
        esp_now_add_peer(&peer);

        Serial.println("[Network] Ready");
    }

    void send(uint8_t type, bool state){
        cmd.deviceType = type;
        cmd.active     = state;
        esp_now_send(SENSOR_NODE_MAC, (uint8_t*)&cmd, sizeof(cmd));
    }

    FirebaseData* get(){ return &fbdo; }
    bool ok(){ return Firebase.ready(); }

    void log(const String &s){
        if(ok()) Firebase.RTDB.pushString(&fbdo, PATH_ERROR_LOG, s);
    }
};
