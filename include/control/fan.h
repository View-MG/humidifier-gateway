#pragma once
#include "gateway.h"
#include <time.h>

class FanLogic {
private:
    GatewayNetwork* net;

    // ---------- CONFIG จาก Firebase ----------
    String mode = "manual";   // "manual" / "auto"
    bool   manual = false;    // manual_state
    int    target = 60;       // humidity target

    // schedule
    bool schedEnable   = false;
    int  schedStartMin = -1;  // นาทีจากเที่ยงคืน
    int  schedStopMin  = -1;

    // ใช้ตรวจ user override (เปลี่ยน mode/manual)
    String prevMode   = "manual";
    bool   prevManual = false;

    // countdown
    unsigned long lastCountdownUpdate = 0;

    // ---------- STATE ภายใน ----------
    bool lastCmd      = false;  // คำสั่งล่าสุดที่ส่งไป Sensor
    bool lastFb       = false;  // feedback ล่าสุดจาก Sensor
    unsigned long lastCfg    = 0;
    unsigned long lastSend   = 0;
    unsigned long lastCheck  = 0;
    unsigned long lastFbTime = 0;

    // auto-recovery จาก mismatch
    uint8_t mismatchCount      = 0;
    const uint8_t MAX_RECOVERY = 3;

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
            // ปกติ 12:00 → 17:00
            return (nowMin >= schedStartMin && nowMin < schedStopMin);
        } else {
            // (ยังไม่ได้รองรับเคสข้ามเที่ยงคืนแบบละเอียด ใช้เคสปกติเป็นหลัก)
            return (nowMin >= schedStartMin || nowMin < schedStopMin);
        }
    }

    // ---------- countdown ----------
    // ก่อนถึง start → นับถอยหลังถึง start
    // ระหว่าง schedule ทำงาน → นับถอยหลังถึง stop
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
            // ยังไม่ถึงเวลา start → นับถอยหลังถึง start
            if (nowSec <= startSec)
                diff = startSec - nowSec;
            else
                diff = 0;   // เลย stop แล้ว
        }

        if (diff < 0) diff = 0;

        bool shouldUpdate = false;

        if (diff > 60) {
            // มากกว่า 1 นาที → อัปเดตทุกนาที (ตอนวินาที=0)
            if (s == 0 && millis() - lastCountdownUpdate > 800) {
                shouldUpdate = true;
            }
        } else {
            // นาทีสุดท้าย → อัปเดตทุก 10 วินาที
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

public:
    FanLogic(GatewayNetwork* n) : net(n) {}

    void update(time_t now, SensorPacket &d) {
        if (!net->ok()) return;
        FirebaseData* fb = net->get();

        // ---------- 1) อ่าน config จาก Firebase ทุก 2 วิ ----------
        if (millis() - lastCfg > 2000) {
            if (Firebase.RTDB.getString(fb, PATH_FAN_MODE))
                mode = fb->stringData();

            if (Firebase.RTDB.getBool(fb, PATH_FAN_MANUAL))
                manual = fb->boolData();

            if (Firebase.RTDB.getInt(fb, PATH_TARGET_HUMIDITY))
                target = fb->intData();

            // schedule.enable  (ไม่ reset เป็น false ถ้าอ่านไม่สำเร็จ)
            if (Firebase.RTDB.getBool(fb, PATH_SCHED_ENABLE)) {
                schedEnable = fb->boolData();
            }

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
                Serial.println("[Schedule] Cancelled by user override (Fan)");
            }

            lastCfg = millis();
        }

        // ---------- 2) schedule window + countdown ----------
        bool inWin = inScheduleWindow(now);
        updateCountdown(now, fb, inWin);

        // ---------- 3) คำนวณ state ที่ “ควร” เป็น ----------
        bool want = false;

        if (schedEnable && inWin) {
            // ถึงเวลาตั้งเวลา → บังคับเปิด
            want = true;
        } else {
            // ปกติ: ทำตาม mode
            if (mode == "manual") {
                want = manual;
            } else if (mode == "auto") {
                want = (d.humidity < target);  // humidity ต่ำกว่า target → เปิด
            } else {
                want = false;
            }
        }

        // ---------- 4) ส่งคำสั่งไป Sensor ----------
        if (want != lastCmd || millis() - lastSend > 3000) {
            net->send(CMD_FAN, want);
            lastCmd   = want;
            lastSend  = millis();
            lastCheck = millis();

            Serial.printf("[Gateway→Sensor] (FAN) CMD=%s (mode=%s, sched_enable=%s, sched_now=%s)\n",
                          want ? "ON" : "OFF",
                          mode.c_str(),
                          schedEnable ? "ON" : "OFF",
                          (schedEnable && inWin) ? "ON" : "OFF");
        }

        // ---------- 5) feedback จาก Sensor ----------
        if (d.fanStatus != lastFb) {
            Firebase.RTDB.setBool(fb, PATH_FAN_STATUS, d.fanStatus);
            lastFb    = d.fanStatus;
            lastFbTime = millis();
            Serial.printf("[Sensor→Gateway] (FAN) REAL=%s\n",
                          d.fanStatus ? "ON" : "OFF");
        }

        // ---------- 6) mismatch + auto recovery ----------
        if (millis() - lastCheck > 900) {

            if (want != d.fanStatus) {

                if (mismatchCount < MAX_RECOVERY) {
                    mismatchCount++;
                    Serial.printf("⚠ FAN MISMATCH - attempt %d, resend CMD=%s\n",
                                  mismatchCount, want ? "ON" : "OFF");

                    net->send(CMD_FAN, want);
                    lastSend  = millis();
                    lastCheck = millis();
                } else {
                    Serial.println("❗ FAN MISMATCH PERSIST - trusting Sensor and syncing state");

                    bool real = d.fanStatus;
                    lastCmd = real;
                    mismatchCount = 0;

                    // ถ้าอยู่ใน manual → sync manual_state ให้ตรงกับของจริง
                    if (mode == "manual") {
                        manual = real;
                        Firebase.RTDB.setBool(fb, PATH_FAN_MANUAL, real);
                        Serial.printf("[AUTO-SYNC] Update PATH_FAN_MANUAL to %s\n",
                                      real ? "true" : "false");
                    }

                    lastCheck = millis();
                }
            } else {
                if (mismatchCount > 0) {
                    Serial.println("[FAN] Mismatch resolved. States are in sync.");
                }
                mismatchCount = 0;
                lastCheck = millis();
            }
        }
    }
};
