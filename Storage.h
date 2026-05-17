#pragma once
#include "config.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

class Storage {
public:
    static bool begin() {
        _mounted = LittleFS.begin(false);
        if (!_mounted) {
            Serial.println("[Storage] LittleFS mount failed");
            return false;
        }
        if (!LittleFS.exists("/config")) LittleFS.mkdir("/config");
        Serial.println("[Storage] OK");
        return true;
    }

    static bool loadAll(AppConfig& cfg) {
        loadWifi(cfg.wifi);
        loadBrain(cfg.brain);
        loadAudio(cfg.audio);
        loadPet(cfg.pet);
        return true;
    }

    static bool saveWifi(const WifiConfig& cfg) {
        JsonDocument doc;
        doc["ssid"]       = cfg.ssid;
        doc["password"]   = cfg.password;
        doc["configured"] = cfg.configured;
        return writeJson(CONFIG_PATH_WIFI, doc);
    }

    static bool loadWifi(WifiConfig& cfg) {
        JsonDocument doc;
        if (!readJson(CONFIG_PATH_WIFI, doc)) return false;
        strlcpy(cfg.ssid,     doc["ssid"]     | "", sizeof(cfg.ssid));
        strlcpy(cfg.password, doc["password"] | "", sizeof(cfg.password));
        cfg.configured = doc["configured"] | false;
        return cfg.configured;
    }

    static bool saveBrain(const BrainConfig& cfg) {
        JsonDocument doc;
        doc["provider"]         = (int)cfg.provider;
        doc["openaiKey"]        = cfg.openaiKey;
        doc["openaiModel"]      = cfg.openaiModel;
        doc["n8nWebhookUrl"]    = cfg.n8nWebhookUrl;
        doc["n8nAuthToken"]     = cfg.n8nAuthToken;
        doc["claudeGatewayUrl"] = cfg.claudeGatewayUrl;
        doc["claudeAuthToken"]  = cfg.claudeAuthToken;
        return writeJson(CONFIG_PATH_BRAIN, doc);
    }

    static bool loadBrain(BrainConfig& cfg) {
        JsonDocument doc;
        if (!readJson(CONFIG_PATH_BRAIN, doc)) return false;
        cfg.provider = (BrainProvider)(doc["provider"] | 0);
        strlcpy(cfg.openaiKey,        doc["openaiKey"]        | "", sizeof(cfg.openaiKey));
        strlcpy(cfg.openaiModel,      doc["openaiModel"]      | "gpt-4o-mini", sizeof(cfg.openaiModel));
        strlcpy(cfg.n8nWebhookUrl,    doc["n8nWebhookUrl"]    | "", sizeof(cfg.n8nWebhookUrl));
        strlcpy(cfg.n8nAuthToken,     doc["n8nAuthToken"]     | "", sizeof(cfg.n8nAuthToken));
        strlcpy(cfg.claudeGatewayUrl, doc["claudeGatewayUrl"] | "", sizeof(cfg.claudeGatewayUrl));
        strlcpy(cfg.claudeAuthToken,  doc["claudeAuthToken"]  | "", sizeof(cfg.claudeAuthToken));
        return true;
    }

    static bool saveAudio(const AudioConfig& cfg) {
        JsonDocument doc;
        doc["whisperEnabled"] = cfg.whisperEnabled;
        doc["ttsEnabled"]     = cfg.ttsEnabled;
        doc["ttsVoice"]       = cfg.ttsVoice;
        doc["ttsSpeed"]       = cfg.ttsSpeed;
        doc["ttsVolume"]      = cfg.ttsVolume;
        doc["openaiKey"]      = cfg.openaiKey;
        return writeJson(CONFIG_PATH_AUDIO, doc);
    }

    static bool loadAudio(AudioConfig& cfg) {
        JsonDocument doc;
        if (!readJson(CONFIG_PATH_AUDIO, doc)) return false;
        cfg.whisperEnabled = doc["whisperEnabled"] | false;
        cfg.ttsEnabled     = doc["ttsEnabled"]     | false;
        strlcpy(cfg.ttsVoice,  doc["ttsVoice"]  | "nova", sizeof(cfg.ttsVoice));
        cfg.ttsSpeed  = doc["ttsSpeed"]  | 1.0f;
        cfg.ttsVolume = doc["ttsVolume"] | 80;
        strlcpy(cfg.openaiKey, doc["openaiKey"] | "", sizeof(cfg.openaiKey));
        return true;
    }

    static bool savePet(const PetConfig& cfg) {
        JsonDocument doc;
        doc["type"] = (int)cfg.type;
        doc["name"] = cfg.name;
        return writeJson(CONFIG_PATH_PET, doc);
    }

    static bool loadPet(PetConfig& cfg) {
        JsonDocument doc;
        if (!readJson(CONFIG_PATH_PET, doc)) return false;
        cfg.type = (PetType)(doc["type"] | 0);
        strlcpy(cfg.name, doc["name"] | "Kraken", sizeof(cfg.name));
        return true;
    }

private:
    static bool readJson(const char* path, JsonDocument& doc) {
        if (!_mounted) return false;
        if (!LittleFS.exists(path)) return false;
        File f = LittleFS.open(path, "r");
        if (!f) return false;
        DeserializationError err = deserializeJson(doc, f);
        f.close();
        return err == DeserializationError::Ok;
    }

    static bool writeJson(const char* path, JsonDocument& doc) {
        if (!_mounted) return false;
        File f = LittleFS.open(path, "w");
        if (!f) return false;
        serializeJson(doc, f);
        f.close();
        return true;
    }

    static bool _mounted;
};

inline bool Storage::_mounted = false;
