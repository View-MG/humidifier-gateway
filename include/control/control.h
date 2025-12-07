#pragma once
#include <Arduino.h>
#include <time.h>
#include <math.h>
#include "gateway.h"
#include "constant.h"
#include <DHT.h>

class ControlLogic {
private:
    GatewayNetwork* net;

    // ---------- CONFIG จาก Firebase ----------
    String mode = "manual";   // "manual" / "auto"
    bool   manual = false;    // manual_state
    int    targetHumid = 60;  // %RH

    // schedule
    bool schedEnable   = false;
    int  schedStartMin = -1;  // นาทีจากเที่ยงคืน
    int  schedStopMin  = -1;

    // ตรวจ user override
    String prevMode   = "manual";
    bool   prevManual = false;

    // ---------- STATE ภายใน ----------
    bool lastCmd      = false;  // คำสั่งล่าสุดที่ส่งไป Sensor
    bool lastFb       = false;  // feedback ล่าสุดจาก Sensor (controlState)
    unsigned long lastCfg    = 0;
    unsigned long lastSend   = 0;
    unsigned long lastCheck  = 0;
    unsigned long lastFbTime = 0;

    uint8_t mismatchCount      = 0;
    const uint8_t MAX_RECOVERY = 3;

    // countdown schedule
    unsigned long lastCountdownUpdate = 0;

    // ---------- ENV (KY-015 DHT11) ----------
    DHT dht;
    bool   envReady      = false;
    float  curTemp       = 0.0f;
    float  curHum        = 0.0f;
    float  lastTempSent  = 0.0f;
    float  lastHumSent   = 0.0f;
    bool   envPushedOnce = false;
    unsigned long lastEnvRead  = 0;
    unsigned long lastEnvPush  = 0;

    // ---------- Sensor push -> Firebase ----------
    SensorPacket lastSensor{};
    bool         hasLastSensor   = false;
    unsigned long lastSensorPush = 0;

    // ---------- helper: เวลา ----------
    int parseHHMM(const String& s) {
        int c = s.indexOf(':');
        if (c < 0) return -1;
        int h = s.substring(0, c).toInt();
        int m = s.substring(c + 1).toInt();
        if (h < 0 || h > 23 || m < 0 || m > 59) return -1;
        return h * 60 + m;
    }

    void getNowHMS(time_t now, int &h, int &m, int &s) {
        struct tm t;
        localtime_r(&now, &t);
        h = t.tm_hour;
        m = t.tm_min;
        s = t.tm_sec;
    }

    int minutesNow(time_t now) {
        int h,m,s;
        getNowHMS(now,h,m,s);
        return h*60 + m;
    }

    bool inScheduleWindow(time_t now) {
        if (!schedEnable) return false;
        if (schedStartMin < 0 || schedStopMin < 0) return false;

        int nowMin = minutesNow(now);
        if (schedStartMin <= schedStopMin) {
            // เคสปกติ 12:00 → 17:00
            return (nowMin >= schedStartMin && nowMin < schedStopMin);
        } else {
            // เคสข้ามเที่ยงคืน (อาจปรับเพิ่มได้ภายหลัง)
            return (nowMin >= schedStartMin || nowMin < schedStopMin);
        }
    }

    // ---------- countdown schedule ----------
    void updateCountdown(time_t now, FirebaseData* fb, bool inWin) {
        if (!schedEnable || schedStartMin < 0 || schedStopMin < 0) return;

        int h,m,s;
        getNowHMS(now,h,m,s);
        int nowSec   = h*3600 + m*60 + s;
        int startSec = schedStartMin * 60;
        int stopSec  = schedStopMin  * 60;

        int diff = 0;

        if (inWin) {
            // กำลังทำงาน → นับถอยหลังถึง stop
            diff = stopSec - nowSec;
        } else {
            // ยังไม่ถึงเวลาเริ่ม → นับไปถึง start
            if (nowSec <= startSec)
                diff = startSec - nowSec;
            else
                diff = 0;   // เลย stop แล้ว (วันนี้)
        }

        if (diff < 0) diff = 0;

        bool shouldUpdate = false;

        if (diff > 60) {
            if (s == 0 && millis() - lastCountdownUpdate > 800) {
                shouldUpdate = true;
            }
        } else {
            if ((diff % 10) == 0 && millis() - lastCountdownUpdate > 800) {
                shouldUpdate = true;
            }
        }

        if (shouldUpdate) {
            Firebase.RTDB.setInt(fb, PATH_SCHED_COUNTDOWN, diff);
            lastCountdownUpdate = millis();
            Serial.printf("[Schedule] Countdown = %d sec\n", diff);
        }
    }

    // ---------- tilt helper ----------
    const char* tiltToText(uint8_t t) {
        if (t == TILT_FALL)    return "FALL";
        if (t == TILT_WARNING) return "WARN";
        return "NORMAL";
    }

    // ---------- อ่าน config จาก Firebase ----------
    void fetchConfig(FirebaseData* fb) {
        if (millis() - lastCfg < CONFIG_POLL_MS) return;

        // mode
        if (Firebase.RTDB.getString(fb, PATH_CTRL_MODE))
            mode = fb->stringData();

        // manual_state
        if (Firebase.RTDB.getBool(fb, PATH_CTRL_MANUAL))
            manual = fb->boolData();

        // humid target
        if (Firebase.RTDB.getInt(fb, PATH_CTRL_TARGET_HUMID))
            targetHumid = fb->intData();

        // schedule enable
        if (Firebase.RTDB.getBool(fb, PATH_SCHED_ENABLE))
            schedEnable = fb->boolData();

        // schedule start/stop
        String sStart, sStop;
        if (Firebase.RTDB.getString(fb, PATH_SCHED_START)) {
            sStart = fb->stringData();
            schedStartMin = parseHHMM(sStart);
        }
        if (Firebase.RTDB.getString(fb, PATH_SCHED_STOP)) {
            sStop = fb->stringData();
            schedStopMin = parseHHMM(sStop);
        }

        // user override → cancel schedule
        bool userOverride = (mode != prevMode) || (manual != prevManual);
        prevMode   = mode;
        prevManual = manual;

        if (userOverride && schedEnable) {
            schedEnable = false;
            Firebase.RTDB.setBool(fb, PATH_SCHED_ENABLE, false);
            Serial.println("[Schedule] Cancelled by user override (Control)");
        }

        lastCfg = millis();
    }

    // ---------- อ่าน DHT11 + push Firebase ----------
    void updateEnv(FirebaseData* fb) {
        if (millis() - lastEnvRead < ENV_POLL_MS) return;
        lastEnvRead = millis();

        float h = dht.readHumidity();
        float t = dht.readTemperature();

        if (isnan(h) || isnan(t)) {
            Serial.println("[Env] DHT read failed");
            return;
        }

        envReady = true;
        curHum   = h;
        curTemp  = t;

        bool push = false;
        if (!envPushedOnce) {
            push = true;
        } else {
            if (fabsf(h - lastHumSent) >= 1.0f ||
                fabsf(t - lastTempSent) >= 0.5f) {
                push = true;
            } else if (millis() - lastEnvPush > SENSOR_PUSH_MS) {
                push = true;
            }
        }

        if (push) {
            Firebase.RTDB.setFloat(fb, PATH_SENSOR_TEMP, curTemp);
            Firebase.RTDB.setFloat(fb, PATH_SENSOR_HUMID, curHum);
            lastTempSent   = curTemp;
            lastHumSent    = curHum;
            lastEnvPush    = millis();
            envPushedOnce  = true;

            Serial.printf("[Env]  T=%.1f°C H=%.1f%%\n", curTemp, curHum);
        }
    }

    // ---------- push Sensor Node data -> Firebase ----------
    void pushSensorToFirebase(const SensorPacket &d, FirebaseData* fb) {
        // ข้ามถ้ายังไม่มี nodeId
        if (d.nodeId == 0) return;

        bool changed = false;

        if (!hasLastSensor ||
            d.waterPercent != lastSensor.waterPercent ||
            d.waterRaw     != lastSensor.waterRaw ||
            d.tiltState    != lastSensor.tiltState ||
            d.controlState != lastSensor.controlState) {
            changed = true;
        }

        bool timeUp = (millis() - lastSensorPush > SENSOR_PUSH_MS);

        if (!changed && !timeUp) return;

        // water
        Firebase.RTDB.setInt(fb, PATH_SENSOR_WATER_PCT, d.waterPercent);
        Firebase.RTDB.setInt(fb, PATH_SENSOR_WATER_RAW, d.waterRaw);

        // tilt
        Firebase.RTDB.setInt(fb, PATH_SENSOR_TILT_STATE, d.tiltState);
        Firebase.RTDB.setString(fb, PATH_SENSOR_TILT_STATE_TXT, tiltToText(d.tiltState));

        // control state (feedback)
        Firebase.RTDB.setBool(fb, PATH_SENSOR_CONTROL_STATE, d.controlState);

        // keyPress: log เฉพาะตอนมีการกดจริง ๆ
        if (d.keyPress != 0 && d.keyPress != lastSensor.keyPress) {
            String keyStr; keyStr += d.keyPress;
            Firebase.RTDB.setString(fb, PATH_SENSOR_KEY_LAST, keyStr);
        }

        lastSensor     = d;
        hasLastSensor  = true;
        lastSensorPush = millis();
    }

public:
    ControlLogic(GatewayNetwork* n)
        : net(n), dht(DHT_PIN, DHT_TYPE) {}

    void begin() {
        dht.begin();
        Serial.println("[Env] DHT11 init");
    }

    void update(time_t now, SensorPacket &d) {
        if (!net->ok()) return;
        FirebaseData* fb = net->get();

        // 1) อ่าน config จาก Firebase
        fetchConfig(fb);

        // 2) อ่าน DHT11 + push env ขึ้น Firebase
        updateEnv(fb);

        // 3) push ข้อมูลจาก Sensor Node ขึ้น Firebase
        pushSensorToFirebase(d, fb);

        // 4) คำนวณ safety
        bool unsafe = false;
        String unsafeReason;

        if (d.nodeId != 0) {
            if (d.waterPercent <= CONTROL_WATER_EMPTY_PCT) {
                unsafe = true;
                unsafeReason += "WATER_EMPTY";
            }
            if (d.tiltState == TILT_FALL) {
                if (unsafeReason.length()) unsafeReason += "+";
                unsafeReason += "TILT_FALL";
                unsafe = true;
            }
        }

        // 5) schedule window + countdown
        bool inWin = inScheduleWindow(now);
        updateCountdown(now, fb, inWin);

        // 6) ตัดสินใจ state ที่ควรจะเป็น
        bool want = false;

        if (unsafe) {
            want = false;    // safety สำคัญสุด
        } else if (schedEnable && inWin) {
            want = true;     // ถึงเวลาตั้งเวลา → บังคับเปิด
        } else {
            if (mode == "manual") {
                want = manual;
            } else if (mode == "auto") {
                bool hasHumidity = envReady && !isnan(curHum) && curHum > 0;
                if (hasHumidity) {
                    // แห้งกว่า target → เปิด
                    want = (curHum < (float)targetHumid);
                } else {
                    want = false; // ไม่มีค่า humidity → ปิดไว้ก่อนเพื่อความปลอดภัย
                }
            } else {
                want = false;
            }
        }

        const char* safeStr     = unsafe ? "BLOCK" : "OK";
        const char* schedNowStr = (schedEnable && inWin) ? "ON" : "OFF";

        // 7) ส่งคำสั่งไป Sensor Node (CMD_CONTROL)
        if (want != lastCmd || millis() - lastSend > CMD_HEARTBEAT_MS) {
            net->send(CMD_CONTROL, want);
            lastCmd   = want;
            lastSend  = millis();
            lastCheck = millis();

            Serial.printf("[Sent]  CMD=%s (mode=%s, sched_en=%s, sched_now=%s, safe=%s)\n",
                          want ? "ON" : "OFF",
                          mode.c_str(),
                          schedEnable ? "ON" : "OFF",
                          schedNowStr,
                          safeStr);

            if (unsafe) {
                Serial.printf("[SAFETY] reason=%s (water=%d%%, tilt=%s)\n",
                              unsafeReason.c_str(),
                              d.waterPercent,
                              tiltToText(d.tiltState));
            }
        }

        // 8) feedback จาก Sensor: sync control_state
        bool fbState = d.controlState;
        if (fbState != lastFb) {
            Firebase.RTDB.setBool(fb, PATH_CTRL_STATE, fbState);
            lastFb     = fbState;
            lastFbTime = millis();
        }

        // 9) mismatch + auto recovery
        if (millis() - lastCheck > 900) {
            if (want != fbState) {
                if (mismatchCount < MAX_RECOVERY) {
                    mismatchCount++;
                    Serial.printf("⚠ CONTROL MISMATCH - attempt %d, resend CMD=%s\n",
                                  mismatchCount, want ? "ON" : "OFF");

                    net->send(CMD_CONTROL, want);
                    lastSend  = millis();
                    lastCheck = millis();
                } else {
                    Serial.println("❗ CONTROL MISMATCH PERSIST - trusting Sensor and syncing state");

                    bool real = fbState;
                    lastCmd = real;
                    mismatchCount = 0;

                    // ถ้าอยู่ใน manual → sync manual_state ให้ตรงกับของจริง
                    if (mode == "manual") {
                        manual = real;
                        Firebase.RTDB.setBool(fb, PATH_CTRL_MANUAL, real);
                        Serial.printf("[AUTO-SYNC] Update PATH_CTRL_MANUAL to %s\n",
                                      real ? "true" : "false");
                    }

                    lastCheck = millis();
                }
            } else {
                if (mismatchCount > 0) {
                    Serial.println("[CONTROL] Mismatch resolved. States are in sync.");
                }
                mismatchCount = 0;
                lastCheck = millis();
            }
        }
    }
};
