#pragma once
#include <WiFi.h>
#include "config.h"

class WifiManager {
public:
    static bool connectToSaved(const WifiConfig& cfg, uint32_t timeoutMs = 10000) {
        if (!cfg.configured || strlen(cfg.ssid) == 0) {
            Serial.println("[WiFi] No saved config");
            return false;
        }
        Serial.printf("[WiFi] Connecting to %s...\n", cfg.ssid);
        WiFi.mode(WIFI_STA);
        WiFi.begin(cfg.ssid, cfg.password);
        uint32_t start = millis();
        while (WiFi.status() != WL_CONNECTED) {
            if (millis() - start > timeoutMs) {
                Serial.println("\n[WiFi] Timeout");
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
        WiFi.mode(WIFI_AP);
        WiFi.softAP(KRAKBOT_AP_SSID, KRAKBOT_AP_PASS);
        _apMode = true;
        Serial.printf("[WiFi] AP: %s  IP: %s\n",
            KRAKBOT_AP_SSID, WiFi.softAPIP().toString().c_str());
    }

    static bool isAPMode() { return _apMode; }

    static String localIP() {
        return _apMode
            ? WiFi.softAPIP().toString()
            : WiFi.localIP().toString();
    }

private:
    static bool _apMode;
};

// Definición del static — inline evita "multiple definition" en Arduino IDE
inline bool WifiManager::_apMode = false;
