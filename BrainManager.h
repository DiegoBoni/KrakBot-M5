#pragma once
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "config.h"

// ─────────────────────────────────────────────
// BrainManager — envía mensajes al provider
// configurado y retorna la respuesta
// ─────────────────────────────────────────────

class BrainManager {
public:
    BrainManager() {}

    void setConfig(const BrainConfig& cfg) { _cfg = &cfg; }

    String chat(const String& userMessage) {
        _lastError = "";
        if (!_cfg) { _lastError = "No config"; return ""; }
        if (WiFi.status() != WL_CONNECTED) {
            _lastError = "Sin WiFi";
            return "";
        }
        switch (_cfg->provider) {
            case BRAIN_OPENAI: return chatOpenAI(userMessage);
            case BRAIN_N8N:    return chatN8N(userMessage);
            case BRAIN_CLAUDE: return chatClaude(userMessage);
        }
        return "Brain no configurado.";
    }

    String lastError() const { return _lastError; }

private:
    const BrainConfig* _cfg = nullptr;
    String _lastError;

    static constexpr const char* SYSTEM_PROMPT =
        "Sos KRAKBOT, un companion IA portátil. "
        "Respondé siempre en español, de forma concisa y amigable. "
        "Máximo 2-3 oraciones. Sin listas ni markdown.";

    static String trimmed(const char* value) {
        String out = value ? String(value) : String();
        out.trim();
        return out;
    }

    String chatOpenAI(const String& msg) {
        String apiKey = trimmed(_cfg->openaiKey);
        if (apiKey.isEmpty()) {
            _lastError = "Falta API key OpenAI";
            return "";
        }
        JsonDocument doc;
        doc["model"]      = _cfg->openaiModel;
        doc["max_tokens"] = 200;
        JsonArray messages = doc["messages"].to<JsonArray>();
        JsonObject sys  = messages.add<JsonObject>();
        sys["role"]     = "system";
        sys["content"]  = SYSTEM_PROMPT;
        JsonObject user = messages.add<JsonObject>();
        user["role"]    = "user";
        user["content"] = msg;

        String body;
        serializeJson(doc, body);
        String auth = String("Bearer ") + apiKey;
        String resp = httpPost("https://api.openai.com/v1/chat/completions",
                               body, auth.c_str());
        if (resp.isEmpty()) return "";

        JsonDocument r;
        if (deserializeJson(r, resp) != DeserializationError::Ok) {
            _lastError = "JSON parse error";
            return "";
        }
        if (r.containsKey("error")) {
            _lastError = r["error"]["message"] | "OpenAI error";
            return "";
        }
        return String(r["choices"][0]["message"]["content"] | "");
    }

    String chatN8N(const String& msg) {
        if (strlen(_cfg->n8nWebhookUrl) == 0) {
            _lastError = "Falta URL N8N";
            return "";
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

    String chatClaude(const String& msg) {
        if (strlen(_cfg->claudeGatewayUrl) == 0) {
            _lastError = "Falta URL del Gateway";
            return "";
        }
        JsonDocument doc;
        doc["message"] = msg;
        doc["source"]  = "krakbot";
        doc["system"]  = SYSTEM_PROMPT;
        String body;
        serializeJson(doc, body);

        String auth = "";
        if (strlen(_cfg->claudeAuthToken) > 0)
            auth = String("Bearer ") + _cfg->claudeAuthToken;

        String resp = httpPost(_cfg->claudeGatewayUrl, body,
                               auth.isEmpty() ? nullptr : auth.c_str());
        if (resp.isEmpty()) return "";

        JsonDocument r;
        if (deserializeJson(r, resp) == DeserializationError::Ok) {
            for (auto k : {"text","content","response","output"})
                if (r.containsKey(k)) return String(r[k] | "");
            return resp.substring(0, 300);
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
