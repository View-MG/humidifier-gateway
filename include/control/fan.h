#pragma once
#include <Arduino.h>
#include "constant.h"
#include "gateway.h"
#include <time.h>

class FanLogic {
private:
    GatewayNetwork* _network;
    String mode;
    bool manualState;
    int targetHumid;
    long schedStart;
    long schedEnd;
    bool _lastCommandState; // จำสถานะล่าสุดที่เราสั่งไป

public:
    FanLogic(GatewayNetwork* network) : _network(network), _lastCommandState(false) {}

    void update(time_t now, SensorPacket& sensorData) {
        if (!_network->isReady()) return;
        FirebaseData* fb = _network->getFBDO();

        // 1. ดึง Config (Optimization: ดึงทีละตัวหรือดึงเป็น JSON ก็ได้)
        // เพื่อความง่าย ดึงทีละตัวตาม Path
        if (Firebase.RTDB.getString(fb, PATH_FAN_MODE)) mode = fb->stringData();
        if (Firebase.RTDB.getBool(fb, PATH_FAN_MANUAL)) manualState = fb->boolData();
        if (Firebase.RTDB.getInt(fb, PATH_FAN_TARGET_HUMID)) targetHumid = fb->intData();
        // Time... (ดึงมาแปลงเป็น Long)

        // 2. คำนวณ Target State
        bool targetState = false;

        if (mode == "manual") {
            targetState = manualState;
        } 
        else if (mode == "auto") {
            // ถ้าความชื้นปัจจุบัน < เป้าหมาย -> เปิดพัดลม
            if (sensorData.humidity < targetHumid) targetState = true;
        }
        // ... (Logic Schedule ตามเวลา) ...

        // 3. สั่งงาน (ส่ง ESP-NOW)
        // ส่งเฉพาะเมื่อสถานะเปลี่ยน หรือส่งย้ำๆ ทุกระยะก็ได้ (ในที่นี้ส่งทุกรอบเพื่อให้ Sensor Node ได้รับชัวร์ๆ)
        _network->sendCommand(CMD_FAN, targetState);
        _lastCommandState = targetState;

        // 4. Recheck / Feedback Loop
        // ถ้าสถานะจริง (sensorData.fanStatus) ไม่ตรงกับที่เราสั่ง (_lastCommandState)
        // อาจจะเกิด Error หรือ Sensor Node ไม่ทำงาน
        if (sensorData.fanStatus != _lastCommandState) {
            String msg = "Fan Error: Command=" + String(_lastCommandState) + " but Status=" + String(sensorData.fanStatus);
            Serial.println(msg);
            _network->logError(msg); // แจ้งเตือนเข้า Firebase
        }

        // 5. อัปเดตสถานะจริงขึ้น Firebase เพื่อให้ App เห็น
        Firebase.RTDB.setBool(fb, PATH_FAN_STATUS, sensorData.fanStatus);
    }
};