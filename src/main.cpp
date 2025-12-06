#include <Arduino.h>
#include <time.h>
#include "constant.h"
#include "gateway.h"
#include "control/fan.h"
#include "control/steam.h"
#include "audio.h" 
#include "log.h"   

// --- Objects ---
GatewayNetwork network;
AudioService audio;
FanLogic fanLogic(&network);
SteamLogic steamLogic(&network);
LogService logger(&network); 

// --- Data ---
SensorPacket currentSensorData;
bool isSensorDataNew = false;

// --- Timers ---
unsigned long lastLogicTime = 0;
unsigned long lastLogTime = 0;

// ==========================================
// 🚀 FreeRTOS Task: สำหรับ Audio โดยเฉพาะ
// ==========================================
TaskHandle_t AudioTaskHandle;

void AudioTask(void * parameter) {
    Serial.println("[System] Audio Task Started on Core 0");
    
    // Loop นี้จะทำงานแยกอิสระ ไม่สน main loop
    while(true) {
        if (ENABLE_AUDIO_STREAM) {
            audio.loop(); 
        }
        
        // ใส่ delay สั้นมากๆ เพื่อให้ Watchdog Timer ไม่ทำงานผิดพลาด (สำคัญ)
        // 1 tick ประมาณ 1ms ซึ่งอาจจะทำให้เสียงขาดนิดหน่อย
        // แต่ถ้าไม่ใส่เลย Task อาจจะกิน CPU จนระบบรวน
        // ลองใส่ 1 ก่อน ถ้าเสียงกระตุก ให้ลองเอาออก หรือใช้ vTaskDelay(0);
        vTaskDelay(1 / portTICK_PERIOD_MS); 
    }
}

void setup() {
    Serial.begin(115200);

    // 1. Init Network
    network.begin();

    // 2. Init Audio Hardware
    if (ENABLE_AUDIO_STREAM) {
        audio.begin();
    }

    // 3. Init Time
    configTime(7 * 3600, 0, "pool.ntp.org");
    Serial.println("[Gateway] System Started");

    // -----------------------------------------------------------
    // 4. สร้าง Task แยกไปรันที่ Core 0
    // -----------------------------------------------------------
    if (ENABLE_AUDIO_STREAM) {
        xTaskCreatePinnedToCore(
            AudioTask,      // ฟังก์ชัน Task
            "AudioTask",    // ชื่อ Task
            10000,          // Stack Size (10kb น่าจะพอ)
            NULL,           // Parameter
            1,              // Priority (1 = สูงกว่า Idle)
            &AudioTaskHandle, // Handle
            0               // Run on Core 0 (Main loop อยู่ Core 1)
        );
    }
}

void loop() {
    // -----------------------------------------------------------
    // Core 1: ทำงาน Logic + Firebase + ESP-NOW
    // (Audio ถูกย้ายออกไปแล้ว ไม่ต้องใส่ตรงนี้)
    // -----------------------------------------------------------

    time_t now = time(nullptr);

    // 1. Process Logic (ทุก 1 วินาที)
    if (millis() - lastLogicTime > LOGIC_INTERVAL_MS) {
        lastLogicTime = millis();
        
        // ช่วงนี้ Firebase อาจจะดึงเวลาไป 1-2 วิ
        // แต่ Audio บน Core 0 จะยังทำงานต่อได้ ไม่หลุด!
        fanLogic.update(now, currentSensorData);
        steamLogic.update(now, currentSensorData);
        
        isSensorDataNew = false; 
    }

    // 2. Data Logging (ทุก 30 วินาที)
    // if (millis() - lastLogTime > LOG_INTERVAL_MS) {
    //     lastLogTime = millis();
    //     logger.writeLog(currentSensorData); 
    // }
}