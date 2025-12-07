#include <Arduino.h>
#include <time.h>
#include "constant.h"
#include "gateway.h"
#include "control/control.h"
#include "audio.h"

// ===== Global shared with gateway.h =====
SensorPacket currentSensorData;
bool isSensorDataNew = false;

// ===== Objects =====
GatewayNetwork network;
AudioService   audio;
ControlLogic   control(&network);

TaskHandle_t AudioTaskHandle;

// AUDIO TASK (CORE 0)
void AudioTask(void * parameter) {
    Serial.println("[Audio] Task Running on CORE 0");
    while (true) {
        audio.loop();
        vTaskDelay(1);   // ป้องกัน WDT
    }
}

void setup() {
    Serial.begin(115200);
    delay(500);

    network.begin();   // WiFi + Firebase + ESP-NOW
    control.begin();   // init DHT11

    if (ENABLE_AUDIO_STREAM) {
        audio.begin(); // I2S + WebSocket

        xTaskCreatePinnedToCore(
            AudioTask,
            "AudioTask",
            10000,
            NULL,
            1,
            &AudioTaskHandle,
            0        // CORE 0
        );
    }

    configTime(7*3600, 0, "pool.ntp.org");
    Serial.println("\n[System] Boot Completed");
}

void loop() {
    static unsigned long lastLogic = 0;

    if (millis() - lastLogic > LOGIC_INTERVAL_MS) {
        lastLogic = millis();

        time_t now = time(nullptr);
        // ใช้ currentSensorData ตัวเดียว (update จาก ESP-NOW callback)
        control.update(now, currentSensorData);
    }
}
