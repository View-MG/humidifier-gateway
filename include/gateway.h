#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <Firebase_ESP_Client.h>

// Addons
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

#include "constant.h"
#include <time.h>

extern SensorPacket currentSensorData;
extern bool isSensorDataNew; 

class GatewayNetwork {
private:
    FirebaseData fbdo;
    FirebaseAuth auth;
    FirebaseConfig config;
    CommandPacket _cmdPacket;

    static void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
        if (len == sizeof(SensorPacket)) {
            memcpy(&currentSensorData, incomingData, sizeof(SensorPacket));
            isSensorDataNew = true;
        }
    }

public:
    GatewayNetwork() {}

    void begin() {
        // 1. WiFi Setup
        WiFi.mode(WIFI_AP_STA);
        
        // 🔥 [สำคัญมาก] ปิดโหมดประหยัดพลังงาน เพื่อให้ Audio Stream ลื่นไหล
        WiFi.setSleep(false); 
        
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        
        Serial.print("[Gateway] Connecting WiFi");
        unsigned long start = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
            delay(500); Serial.print(".");
        }
        Serial.println("\n[Gateway] WiFi Connected");

        // 2. Time Sync (สำหรับ SSL)
        configTime(7 * 3600, 0, "pool.ntp.org", "time.nist.gov");
        Serial.print("[Gateway] Syncing Time");
        time_t now = time(nullptr);
        while (now < 1600000000) { 
            delay(500); Serial.print("."); now = time(nullptr);
        }
        Serial.println("\n[Gateway] Time Synced");

        // 3. Firebase Setup
        config.api_key = FIREBASE_API_KEY;
        config.database_url = FIREBASE_DATABASE_URL;
        
        // ตั้ง Timeout ให้นานขึ้น
        config.timeout.wifiReconnect = 10 * 1000;
        config.timeout.socketConnection = 10 * 1000;
        config.timeout.sslHandshake = 10 * 1000;
        config.timeout.serverResponse = 10 * 1000;
        
        config.token_status_callback = tokenStatusCallback;

        if (Firebase.signUp(&config, &auth, "", "")) {
            Serial.println("[Firebase] Token OK");
        }
        
        Firebase.begin(&config, &auth);
        Firebase.reconnectWiFi(true);

        // 4. ESP-NOW Setup
        if (esp_now_init() != ESP_OK) {
            Serial.println("[Gateway] ESP-NOW Fail");
            return;
        }
        esp_now_register_recv_cb(OnDataRecv);

        esp_now_peer_info_t peerInfo;
        memset(&peerInfo, 0, sizeof(peerInfo));
        memcpy(peerInfo.peer_addr, SENSOR_NODE_MAC, 6);
        peerInfo.channel = 0; 
        peerInfo.encrypt = false;
        
        if (esp_now_add_peer(&peerInfo) != ESP_OK){
            Serial.println("[Gateway] Add Peer Fail");
        }
        
        Serial.println("[Gateway] Network Ready");
    }

    void sendCommand(uint8_t deviceType, bool active) {
        _cmdPacket.deviceType = deviceType;
        _cmdPacket.active = active;
        esp_now_send(SENSOR_NODE_MAC, (uint8_t *) &_cmdPacket, sizeof(_cmdPacket));
    }

    void logError(String message) {
        if (Firebase.ready()) {
            Firebase.RTDB.pushString(&fbdo, PATH_ERROR_LOG, message);
        }
    }
    
    FirebaseData* getFBDO() { return &fbdo; }
    bool isReady() { return Firebase.ready(); }
};