#pragma once
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "config.h"
#include "Storage.h"

// ─────────────────────────────────────────────
// BrainManager — OpenAI y N8N
// Si soul está activo, usa el contenido de /soul_N.md como system prompt
// ─────────────────────────────────────────────

class BrainManager {
public:
    BrainManager() {}

    void setConfig(const BrainConfig& cfg) { _cfg = &cfg; }
    void setSoul(const SoulConfig* soul, int petIndex) {
        _soul    = soul;
        _petIdx  = petIndex;
    }

    bool hasCredentials() const {
        if (!_cfg) return false;
        switch (_cfg->provider) {
            case BRAIN_OPENAI: return strlen(_cfg->openaiKey) > 0;
            case BRAIN_N8N:    return strlen(_cfg->n8nWebhookUrl) > 0;
        }
        return false;
    }

    String chat(const String& userMessage) {
        _lastError = "";
        if (!_cfg) { _lastError = "No config"; return ""; }
        if (!hasCredentials()) { _lastError = "Sin credenciales. Configura el brain en el panel web."; return ""; }
        if (WiFi.status() != WL_CONNECTED) { _lastError = "Sin WiFi"; return ""; }
        switch (_cfg->provider) {
            case BRAIN_OPENAI: return chatOpenAI(userMessage);
            case BRAIN_N8N:    return chatN8N(userMessage);
        }
        return "Brain no configurado.";
    }

    String lastError() const { return _lastError; }

private:
    const BrainConfig* _cfg     = nullptr;
    const SoulConfig*  _soul    = nullptr;
    int                _petIdx  = 0;
    String             _lastError;

    static constexpr const char* DEFAULT_SYSTEM =
        "Sos KRAKBOT, un companion IA portatil. "
        "Responde siempre en espanol, de forma concisa y amigable. "
        "Maximo 2-3 oraciones. Sin listas ni markdown.";

    // Devuelve el system prompt: soul si está activo, sino el default
    String systemPrompt() {
        if (_soul && _petIdx >= 0 && _petIdx < 5 && _soul->enabled[_petIdx]) {
            String soul = Storage::loadSoul(_petIdx);
            if (!soul.isEmpty()) return soul;
        }
        return String(DEFAULT_SYSTEM);
    }

    static String trimmed(const char* value) {
        String out = value ? String(value) : String();
        out.trim();
        return out;
    }

    String chatOpenAI(const String& msg) {
        String apiKey = trimmed(_cfg->openaiKey);
        if (apiKey.isEmpty()) { _lastError = "Falta API key OpenAI"; return ""; }

        JsonDocument doc;
        doc["model"]      = _cfg->openaiModel;
        doc["max_tokens"] = 200;
        JsonArray messages = doc["messages"].to<JsonArray>();
        JsonObject sys  = messages.add<JsonObject>();
        sys["role"]     = "system";
        sys["content"]  = systemPrompt();
        JsonObject user = messages.add<JsonObject>();
        user["role"]    = "user";
        user["content"] = msg;

        String body;
        serializeJson(doc, body);
        String resp = httpPost("https://api.openai.com/v1/chat/completions",
                               body,
                               (String("Bearer ") + apiKey).c_str());
        if (resp.isEmpty()) return "";

        JsonDocument r;
        if (deserializeJson(r, resp) != DeserializationError::Ok) {
            _lastError = "JSON parse error"; return "";
        }
        if (r.containsKey("error")) {
            _lastError = r["error"]["message"] | "OpenAI error"; return "";
        }
        return String(r["choices"][0]["message"]["content"] | "");
    }

    String chatN8N(const String& msg) {
        if (strlen(_cfg->n8nWebhookUrl) == 0) {
            _lastError = "Falta URL N8N"; return "";
        }
        JsonDocument doc;
        doc["message"] = msg;
        doc["source"]  = "krakbot";
        String body;
        serializeJson(doc, body);

        String auth = "";
        if (strlen(_cfg->n8nAuthToken) > 0)
            auth = String("Bearer ") + _cfg->n8nAuthToken;

        String resp = httpPost(_cfg->n8nWebhookUrl, body,
                               auth.isEmpty() ? nullptr : auth.c_str());
        if (resp.isEmpty()) return "";

        JsonDocument r;
        if (deserializeJson(r, resp) == DeserializationError::Ok) {
            for (auto k : {"text","response","output","message","reply"})
                if (r.containsKey(k)) return String(r[k] | "");
            if (r.containsKey("choices"))
                return String(r["choices"][0]["message"]["content"] | "");
            return resp.substring(0, 200);
        }
        return resp;
    }

    String httpPost(const char* url, const String& body, const char* auth = nullptr) {
        HTTPClient http;
        http.begin(url);
        http.addHeader("Content-Type", "application/json");
        if (auth) http.addHeader("Authorization", auth);
        http.setTimeout(15000);
        int code = http.POST(body);
        if (code < 200 || code >= 300) {
            _lastError = "HTTP " + String(code);
            http.end();
            return "";
        }
        String result = http.getString();
        http.end();
        return result;
    }
};
