#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <Firebase_ESP_Client.h>

class ConfigService {
public:
    void beginWiFi();
    void beginFirebase();

    void sendFloat(const String& path, float value);
    float readFloat(const String& path);
    
    void sendBool(const String& path, bool value);
    bool readBool(const String& path);

    String readString(const String& path);
    void sendString(const String &path, const String &value);

    FirebaseData fbdo;
private:
    FirebaseAuth auth;
    FirebaseConfig config;
};
