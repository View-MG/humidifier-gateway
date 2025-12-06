#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include "constant.h" // ดึง Config WiFi/Firebase จากที่นี่

// Addons สำหรับจัดการ Token
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

class ConfigService {
public:
    // เปิดให้เข้าถึง fbdo ได้โดยตรง (เพื่อส่งให้ LogService ใช้งาน)
    FirebaseData fbdo;

private:
    FirebaseAuth auth;
    FirebaseConfig config;

public:
    ConfigService() {}

    // --- เริ่มต้น WiFi ---
    void beginWiFi() {
        // ใช้ AP_STA เพื่อให้ ESP-NOW ทำงานเสถียรพร้อม WiFi
        WiFi.mode(WIFI_AP_STA);
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

        Serial.print("[WiFi] Connecting");
        unsigned long start = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
            Serial.print(".");
            delay(500);
        }

        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("\n[WiFi] Connected!");
            Serial.print("[WiFi] IP: "); Serial.println(WiFi.localIP());
        } else {
            Serial.println("\n[WiFi] Connection Failed");
        }
    }

    // --- เริ่มต้น Firebase ---
    void beginFirebase() {
        Serial.println("[Firebase] Init...");
        config.api_key = FIREBASE_API_KEY;
        config.database_url = FIREBASE_DATABASE_URL;

        if (Firebase.signUp(&config, &auth, "", "")) {
            Serial.println("[Firebase] Token OK");
        } else {
            Serial.printf("[Firebase] Sign-up Error: %s\n", config.signer.signupError.message.c_str());
        }

        config.token_status_callback = tokenStatusCallback;
        Firebase.begin(&config, &auth);
        Firebase.reconnectWiFi(true);
    }

    // --- Helper: Float ---
    void sendFloat(const String& path, float value) {
        if (Firebase.ready()) Firebase.RTDB.setFloat(&fbdo, path, value);
    }

    float readFloat(const String& path) {
        if (Firebase.ready() && Firebase.RTDB.getFloat(&fbdo, path)) {
            return fbdo.floatData();
        }
        return 0.0f;
    }

    // --- Helper: Bool ---
    void sendBool(const String& path, bool value) {
        if (Firebase.ready()) Firebase.RTDB.setBool(&fbdo, path, value);
    }

    bool readBool(const String& path) {
        if (Firebase.ready() && Firebase.RTDB.getBool(&fbdo, path)) {
            return fbdo.boolData();
        }
        return false;
    }

    // --- Helper: String ---
    void sendString(const String &path, const String &value) {
        if (Firebase.ready()) Firebase.RTDB.setString(&fbdo, path, value);
    }

    String readString(const String& path) {
        if (Firebase.ready() && Firebase.RTDB.getString(&fbdo, path)) {
            return fbdo.stringData();
        }
        return "";
    }
};