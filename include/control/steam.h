#pragma once
#include "gateway.h"
#include <time.h>

class SteamLogic {
private:
    GatewayNetwork* net;

    // ---------- CONFIG ----------
    String mode = "manual";   // "manual" / "auto"
    bool   manual = false;
    int    target = 60;       // humidity target

    // schedule
    bool schedEnable   = false;
    int  schedStartMin = -1;
    int  schedStopMin  = -1;

    String prevMode   = "manual";
    bool   prevManual = false;

    bool lastCmd      = false;
    bool lastFb       = false;
    unsigned long lastCfg    = 0;
    unsigned long lastSend   = 0;
    unsigned long lastCheck  = 0;
    unsigned long lastFbTime = 0;

    uint8_t mismatchCount      = 0;
    const uint8_t MAX_RECOVERY = 3;

    // countdown
    unsigned long lastCountdownUpdate = 0;

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
            return (nowMin >= schedStartMin && nowMin < schedStopMin);
        } else {
            // ข้ามเที่ยงคืน (ยังไม่รองรับละเอียด ใช้เคสปกติเป็นหลัก)
            return (nowMin >= schedStartMin || nowMin < schedStopMin);
        }
    }

    // ---------- countdown ----------
    void updateCountdown(time_t now, FirebaseData* fb, bool inWin) {
        if (!schedEnable || schedStartMin < 0 || schedStopMin < 0) return;

        int h,m,s;
        getNowHMS(now,h,m,s);
        int nowSec   = h*3600 + m*60 + s;
        int startSec = schedStartMin * 60;
        int stopSec  = schedStopMin  * 60;

        int diff = 0;

        if (inWin) {
            // อยู่ในช่วง schedule → นับไปจนถึงเวลาปิด
            diff = stopSec - nowSec;
        } else {
            // ยังไม่เริ่ม → นับไปจนถึงเวลาเริ่ม
            if (nowSec <= startSec)
                diff = startSec - nowSec;
            else
                diff = 0;
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

public:
    SteamLogic(GatewayNetwork* n) : net(n) {}

    void update(time_t now, SensorPacket &d) {
        if (!net->ok()) return;
        FirebaseData* fb = net->get();

        // ---------- 1) อ่าน config ----------
        if (millis() - lastCfg > 2000) {
            if (Firebase.RTDB.getString(fb, PATH_STEAM_MODE))
                mode = fb->stringData();

            if (Firebase.RTDB.getBool(fb, PATH_STEAM_MANUAL))
                manual = fb->boolData();

            if (Firebase.RTDB.getInt(fb, PATH_TARGET_HUMIDITY))
                target = fb->intData();

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
                Serial.println("[Schedule] Cancelled by user override (Steam)");
            }

            lastCfg = millis();
        }

        // ---------- 2) schedule window + countdown ----------
        bool inWin = inScheduleWindow(now);
        updateCountdown(now, fb, inWin);

        // ---------- 3) ตัดสินใจ state ----------
        bool want = false;

        if (schedEnable && inWin) {
            want = true;  // บังคับเปิดช่วง schedule
        } else {
            if (mode == "manual") {
                want = manual;
            } else if (mode == "auto") {
                want = (d.humidity < target);
            } else {
                want = false;
            }
        }

        // ---------- 4) ส่งคำสั่ง ----------
        if (want != lastCmd || millis() - lastSend > 3000) {
            net->send(CMD_STEAM, want);
            lastCmd   = want;
            lastSend  = millis();
            lastCheck = millis();

            Serial.printf("[Gateway→Sensor] (STEAM) CMD=%s (mode=%s, sched_enable=%s, sched_now=%s)\n",
                          want ? "ON" : "OFF",
                          mode.c_str(),
                          schedEnable ? "ON" : "OFF",
                          (schedEnable && inWin) ? "ON" : "OFF");
        }

        // ---------- 5) feedback ----------
        if (d.steamStatus != lastFb) {
            Firebase.RTDB.setBool(fb, PATH_STEAM_STATUS, d.steamStatus);
            lastFb    = d.steamStatus;
            lastFbTime = millis();
            Serial.printf("[Sensor→Gateway] (STEAM) REAL=%s\n",
                          d.steamStatus ? "ON" : "OFF");
        }

        // ---------- 6) mismatch + auto recovery ----------
        if (millis() - lastCheck > 900) {

            if (want != d.steamStatus) {

                if (mismatchCount < MAX_RECOVERY) {
                    mismatchCount++;
                    Serial.printf("⚠ STEAM MISMATCH - attempt %d, resend CMD=%s\n",
                                  mismatchCount, want ? "ON" : "OFF");

                    net->send(CMD_STEAM, want);
                    lastSend  = millis();
                    lastCheck = millis();
                } else {
                    Serial.println("❗ STEAM MISMATCH PERSIST - trusting Sensor and syncing state");

                    bool real = d.steamStatus;
                    lastCmd = real;
                    mismatchCount = 0;

                    if (mode == "manual") {
                        manual = real;
                        Firebase.RTDB.setBool(fb, PATH_STEAM_MANUAL, real);
                        Serial.printf("[AUTO-SYNC] Update PATH_STEAM_MANUAL to %s\n",
                                      real ? "true" : "false");
                    }

                    lastCheck = millis();
                }
            } else {
                if (mismatchCount > 0) {
                    Serial.println("[STEAM] Mismatch resolved. States are in sync.");
                }
                mismatchCount = 0;
                lastCheck = millis();
            }
        }
    }
};
