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
        for (int i = 0; i < 5; i++) loadBrain(cfg.brains[i], i);
        loadAudio(cfg.audio);
        loadPet(cfg.pet);
        loadSoulConfig(cfg.soul);
        return true;
    }

    static bool saveAll(AppConfig& cfg) {
        saveWifi(cfg.wifi);
        for (int i = 0; i < 5; i++) saveBrain(cfg.brains[i], i);
        saveAudio(cfg.audio);
        savePet(cfg.pet);
        saveSoulConfig(cfg.soul);
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

    static bool saveBrain(const BrainConfig& cfg, int petIndex) {
        char path[32];
        snprintf(path, sizeof(path), "/config/brain_%d.json", petIndex);
        JsonDocument doc;
        doc["provider"]      = (int)cfg.provider;
        doc["openaiKey"]     = cfg.openaiKey;
        doc["openaiModel"]   = cfg.openaiModel;
        doc["n8nWebhookUrl"] = cfg.n8nWebhookUrl;
        doc["n8nAuthToken"]  = cfg.n8nAuthToken;
        return writeJson(path, doc);
    }

    static bool loadBrain(BrainConfig& cfg, int petIndex) {
        char path[32];
        snprintf(path, sizeof(path), "/config/brain_%d.json", petIndex);
        JsonDocument doc;
        if (!readJson(path, doc)) return false;
        cfg.provider = (BrainProvider)(doc["provider"] | 0);
        if ((int)cfg.provider > 1) cfg.provider = BRAIN_OPENAI;
        strlcpy(cfg.openaiKey,     doc["openaiKey"]     | "", sizeof(cfg.openaiKey));
        strlcpy(cfg.openaiModel,   doc["openaiModel"]   | "gpt-4o-mini", sizeof(cfg.openaiModel));
        strlcpy(cfg.n8nWebhookUrl, doc["n8nWebhookUrl"] | "", sizeof(cfg.n8nWebhookUrl));
        strlcpy(cfg.n8nAuthToken,  doc["n8nAuthToken"]  | "", sizeof(cfg.n8nAuthToken));
        return true;
    }

    // ── Soul por mascota ─────────────────────────────────────────────────────
    static String soulPath(int petIndex) {
        return String("/soul_") + petIndex + ".md";
    }

    static bool saveSoul(int petIndex, const String& content) {
        if (!_mounted) return false;
        String path = soulPath(petIndex);
        File f = LittleFS.open(path, "w");
        if (!f) return false;
        f.print(content);
        f.close();
        return true;
    }

    static String loadSoul(int petIndex) {
        if (!_mounted) return "";
        String path = soulPath(petIndex);
        if (!LittleFS.exists(path)) return "";
        File f = LittleFS.open(path, "r");
        if (!f) return "";
        String out = f.readString();
        f.close();
        return out;
    }

    static bool saveSoulConfig(const SoulConfig& cfg) {
        JsonDocument doc;
        JsonArray arr = doc["enabled"].to<JsonArray>();
        for (int i = 0; i < 5; i++) arr.add(cfg.enabled[i]);
        return writeJson(CONFIG_PATH_SOUL, doc);
    }

    static bool loadSoulConfig(SoulConfig& cfg) {
        JsonDocument doc;
        if (!readJson(CONFIG_PATH_SOUL, doc)) return false;
        JsonArray arr = doc["enabled"].as<JsonArray>();
        int i = 0;
        for (bool v : arr) { if (i < 5) cfg.enabled[i++] = v; }
        return true;
    }

    static bool saveAudio(const AudioConfig& cfg) {
        JsonDocument doc;
        doc["ttsEnabled"] = cfg.ttsEnabled;
        doc["ttsVoice"]   = cfg.ttsVoice;
        doc["ttsVolume"]  = cfg.ttsVolume;
        doc["openaiKey"]  = cfg.openaiKey;
        return writeJson(CONFIG_PATH_AUDIO, doc);
    }

    static bool loadAudio(AudioConfig& cfg) {
        JsonDocument doc;
        if (!readJson(CONFIG_PATH_AUDIO, doc)) return false;
        cfg.ttsEnabled = doc["ttsEnabled"] | false;
        strlcpy(cfg.ttsVoice,  doc["ttsVoice"]  | "nova", sizeof(cfg.ttsVoice));
        cfg.ttsVolume = doc["ttsVolume"] | 70;
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

    // ── Historial de chat por mascota ────────────────────────────────────────
    static String historyPath(PetType pet) {
        const char* names[] = {"kraken","eye","crtbot","drone","blob"};
        String p = "/history_";
        p += names[(int)pet];
        p += ".json";
        return p;
    }

    static bool saveHistory(PetType pet, const ChatEntry* entries, int count) {
        if (!_mounted) return false;
        int start = count > MAX_HISTORY_FS ? count - MAX_HISTORY_FS : 0;
        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();
        for (int i = start; i < count; i++) {
            JsonObject obj = arr.add<JsonObject>();
            obj["role"] = entries[i].role;
            obj["text"] = entries[i].text;
        }
        String path = historyPath(pet);
        File f = LittleFS.open(path, "w");
        if (!f) return false;
        serializeJson(doc, f);
        f.close();
        return true;
    }

    static int loadHistory(PetType pet, ChatEntry* entries, int maxEntries) {
        if (!_mounted) return 0;
        String path = historyPath(pet);
        if (!LittleFS.exists(path)) return 0;
        File f = LittleFS.open(path, "r");
        if (!f) return 0;
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, f);
        f.close();
        if (err != DeserializationError::Ok) return 0;
        JsonArray arr = doc.as<JsonArray>();
        int count = 0;
        for (JsonObject obj : arr) {
            if (count >= maxEntries) break;
            entries[count].role = obj["role"] | "bot";
            entries[count].text = obj["text"] | "";
            count++;
        }
        return count;
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
