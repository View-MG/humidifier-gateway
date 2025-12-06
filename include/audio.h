#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <driver/i2s.h>
#include "constant.h"

// ใช้ define เดียวกับโค้ดต้นฉบับของคุณเพื่อความชัวร์
#define I2S_SAMPLE_RATE   16000
#define I2S_READ_LEN      512 

class AudioService {
public:
    WebSocketsClient ws;
    int32_t i2s_buffer[I2S_READ_LEN]; 
    int16_t pcm16[I2S_READ_LEN / 2];

    void begin() {
        if (!ENABLE_AUDIO_STREAM) return;
        
        Serial.println("[Audio] Init I2S & WebSocket...");
        
        // 1. Init I2S (Copy config จากโค้ดต้นฉบับเป๊ะๆ)
        i2s_config_t cfg = {
            .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
            .sample_rate = I2S_SAMPLE_RATE,
            .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
            .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT, // ต้องรับ Stereo เท่านั้นสำหรับ INMP441
            .communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB),
            .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
            .dma_buf_count = 8,
            .dma_buf_len = 64,
            .use_apll = false,
            .tx_desc_auto_clear = false,
            .fixed_mclk = 0
        };

        i2s_pin_config_t pin = {
            .bck_io_num = I2S_SCK,
            .ws_io_num = I2S_WS,
            .data_out_num = -1,
            .data_in_num = I2S_SD
        };
        pin.mck_io_num = I2S_PIN_NO_CHANGE;

        esp_err_t err = i2s_driver_install(I2S_NUM_0, &cfg, 0, NULL);
        if (err != ESP_OK) Serial.println("[Audio] Failed to install driver");
        
        i2s_set_pin(I2S_NUM_0, &pin);
        i2s_zero_dma_buffer(I2S_NUM_0);
        i2s_start(I2S_NUM_0);

        // 2. Init WebSocket
        connectWS();
    }

    void loop() {
        if (!ENABLE_AUDIO_STREAM) return;
        ws.loop();

        if (!ws.isConnected()) return;

        size_t bytes_read = 0;
        
        // ใช้ Timeout 0 เพื่อให้เป็น Non-blocking (ไม่ขัดขวาง ESP-NOW)
        // แต่ถ้าอ่านไม่ทัน เสียงอาจกระตุกนิดหน่อย (ยอมแลกเพื่อ Multitasking)
        esp_err_t err = i2s_read(I2S_NUM_0, (void*)i2s_buffer, sizeof(i2s_buffer), &bytes_read, 0);

        if (err != ESP_OK || bytes_read == 0) return;

        int samples = bytes_read / 4;     // จำนวน Sample รวม L+R
        int frames = samples / 2;         // จำนวน Frame (คู่ L/R)

        // Logic แปลงข้อมูล (เหมือนโค้ดต้นฉบับ)
        for (int i = 0; i < frames; i++) {
            // เลือก Channel Left (Index * 2)
            // ถ้าไมค์คุณออก Right ให้แก้เป็น [i * 2 + 1]
            int32_t val = i2s_buffer[i * 2]; 

            val = val >> 14; 
            
            // Hard clip เพื่อกันเสียงแตก
            if (val > 32767) val = 32767;
            if (val < -32768) val = -32768;

            pcm16[i] = (int16_t)val;
        }

        // ส่งข้อมูลเป็น Binary
        ws.sendBIN((uint8_t*)pcm16, frames * 2);
    }

private:
    void connectWS() {
        ws.begin(WS_HOST, WS_PORT, WS_PATH);
        ws.setReconnectInterval(2000);
        ws.onEvent([](WStype_t type, uint8_t*, size_t) {
            if (type == WStype_CONNECTED) Serial.println("[Audio] WS Connected 🟢");
            else if (type == WStype_DISCONNECTED) Serial.println("[Audio] WS Disconnected 🔴");
        });
    }
};