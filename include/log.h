#pragma once
#include <Arduino.h>
#include <time.h>
#include "gateway.h"
#include "constant.h"

class LogService {
private:
    GatewayNetwork* network;

    // ฟังก์ชันภายในสำหรับดึงเวลาปัจจุบัน
    String getTimestamp() {
        time_t now = time(nullptr);
        char buf[40];
        // Format: 2023-10-25T14:30:00Z (ISO 8601)
        strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
        return String(buf);
    }

public:
    // Constructor รับ pointer ของ GatewayNetwork เพื่อใช้ส่งข้อมูล
    LogService(GatewayNetwork* net) : network(net) {}

    // ฟังก์ชันหลักสำหรับบันทึก Log ลง Firestore
    void writeLog(SensorPacket& data) {
        // เช็คก่อนว่าเน็ตพร้อมไหม
        if (!network->isReady()) return;

        String ts = getTimestamp();
        String documentPath = "logs/";
        documentPath += ts; // ตั้งชื่อ document ตามเวลา
        
        // แปลง Keypad char เป็น String (เพราะ Firestore ไม่รับ char ตรงๆ)
        String keyStr = (data.keyPress == '\0') ? "" : String(data.keyPress);

        // สร้าง JSON Object สำหรับ Firestore
        FirebaseJson content;
        content.set("fields/temp/doubleValue", data.temperature);
        content.set("fields/humidity/doubleValue", data.humidity);
        content.set("fields/water/doubleValue", data.waterPercent);
        content.set("fields/tilt/booleanValue", data.isTilted);
        content.set("fields/keypad/stringValue", keyStr); 
        content.set("fields/fan_status/booleanValue", data.fanStatus);
        content.set("fields/steam_status/booleanValue", data.steamStatus);
        content.set("fields/created_at/timestampValue", ts);

        // ยิงขึ้น Firestore
        // ใช้ network->getFBDO() เพื่อดึง FirebaseData object จาก GatewayNetwork
        if (Firebase.Firestore.createDocument(network->getFBDO(),
                                              FIREBASE_PROJECT_ID,
                                              "",
                                              documentPath.c_str(),
                                              content.raw()))
        {
            Serial.printf("[Firestore] Log saved: %s\n", ts.c_str());
        }
        else {
            Serial.printf("[Firestore] Error: %s\n", network->getFBDO()->errorReason().c_str());
        }
    }
};