#pragma once
#include <WiFi.h>
#include "config.h"

class WifiManager {
public:
    static void eraseRadioCredentials() {
        WiFi.persistent(true);
        WiFi.disconnect(true, true);
        delay(200);
        WiFi.mode(WIFI_OFF);
        delay(150);
        WiFi.persistent(false);
        _apMode = false;
        Serial.println("[WiFi] Radio credentials erased");
    }

    static bool connectToSaved(const WifiConfig& cfg, uint32_t timeoutMs = 10000) {
        if (!cfg.configured || strlen(cfg.ssid) == 0) {
            Serial.println("[WiFi] No saved config");
            return false;
        }
        Serial.printf("[WiFi] Connecting to %s...\n", cfg.ssid);
        WiFi.persistent(false);
        WiFi.disconnect(true, true);
        delay(100);
        WiFi.setSleep(false);
        WiFi.mode(WIFI_STA);
        WiFi.begin(cfg.ssid, cfg.password);
        uint32_t start = millis();
        while (WiFi.status() != WL_CONNECTED) {
            if (millis() - start > timeoutMs) {
                Serial.println("\n[WiFi] Timeout");
                WiFi.disconnect(true, true);
                return false;
            }
            delay(250);
            Serial.print(".");
        }
        Serial.printf("\n[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
        _apMode = false;
        return true;
    }

    static void startAP() {
        WiFi.disconnect(true, true);
        delay(100);
        WiFi.setSleep(false);
        WiFi.mode(WIFI_AP);
        if (strlen(KRAKBOT_AP_PASS) == 0) {
            WiFi.softAP(KRAKBOT_AP_SSID);
        } else {
            WiFi.softAP(KRAKBOT_AP_SSID, KRAKBOT_AP_PASS);
        }
        delay(150);
        _apMode = true;
        Serial.printf("[WiFi] AP: %s  IP: %s\n",
            KRAKBOT_AP_SSID, WiFi.softAPIP().toString().c_str());
    }

    static void startConfigPortal() {
        startAP();
    }

    static bool isAPMode() { return _apMode; }

    static String localIP() {
        return _apMode
            ? WiFi.softAPIP().toString()
            : WiFi.localIP().toString();
    }

    static String apIP() {
        return WiFi.softAPIP().toString();
    }

    static bool hasStation() {
        return WiFi.status() == WL_CONNECTED;
    }

private:
    static bool _apMode;
};

// Definición del static — inline evita "multiple definition" en Arduino IDE
inline bool WifiManager::_apMode = false;
