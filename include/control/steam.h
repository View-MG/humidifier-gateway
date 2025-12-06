#pragma once
#include <Arduino.h>
#include "constant.h"
#include "gateway.h"

class SteamLogic {
private:
    GatewayNetwork* _network;
    String mode;
    bool manualState;
    int targetHumid;
    
    bool _lastTargetState;
    unsigned long _lastChangeTime;

public:
    SteamLogic(GatewayNetwork* network) 
        : _network(network), _lastTargetState(false), _lastChangeTime(0) {}

    void update(time_t now, SensorPacket& sensorData) {
        if (!_network->isReady()) return;
        FirebaseData* fb = _network->getFBDO();

        // 1. Config
        if (Firebase.RTDB.getString(fb, PATH_STEAM_MODE)) mode = fb->stringData();
        if (Firebase.RTDB.getBool(fb, PATH_STEAM_MANUAL)) manualState = fb->boolData();
        if (Firebase.RTDB.getInt(fb, PATH_STEAM_TARGET_HUMID)) targetHumid = fb->intData();
        
        // 2. Logic
        bool targetState = false;
        String reason = "";

        if (mode == "manual") {
            targetState = manualState;
            reason = "Manual Mode";
        } 
        else if (mode == "auto") {
            if (sensorData.humidity < targetHumid) {
                targetState = true;
                reason = "Auto: Humid(" + String(sensorData.humidity) + ") < Target(" + String(targetHumid) + ")";
            } else {
                reason = "Auto: Humid OK";
            }
        }

        // --- [LOG 1] : แสดงการตัดสินใจเมื่อสถานะเปลี่ยน ---
        if (targetState != _lastTargetState) {
            Serial.println("\n------------------------------------------------");
            Serial.printf("[SteamLogic] 🔄 State Change: %s -> %s\n", 
                          _lastTargetState ? "ON" : "OFF", 
                          targetState ? "ON" : "OFF");
            Serial.println("[SteamLogic] Reason: " + reason);
            Serial.println("------------------------------------------------");

            _lastTargetState = targetState;
            _lastChangeTime = millis();
        }

        // 3. Command
        _network->sendCommand(CMD_STEAM, targetState);

        // 4. Recheck & Update UI
        Firebase.RTDB.setBool(fb, PATH_STEAM_STATUS, sensorData.steamStatus);
    }
};