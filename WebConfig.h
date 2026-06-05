#pragma once
#include <WebServer.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include "config.h"
#include "Storage.h"

// ─────────────────────────────────────────────────────────────────────────────
//  WebConfig — panel web nórdico minimalista, estética verde
//  Chat completo por mascota, brain por pet, audio, wifi
// ─────────────────────────────────────────────────────────────────────────────

class WebConfig {
public:
    WebConfig() : _server(80) {}

    using SaveCallback = std::function<void()>;
    void onSave(SaveCallback cb) { _onSave = cb; }

    // Refs a estado de chat en RAM
    void setRefs(ChatEntry* hist, int* histCount, bool* waiting) {
        _history = hist; _historyCount = histCount; _waitingReply = waiting;
    }

    void begin(AppConfig& cfg) {
        _cfg = &cfg;
        setupRoutes();
        _server.begin();
        Serial.println("[Web] Server started");
    }

    void end()    { _server.stop(); }
    void handle() { _server.handleClient(); }

private:
    WebServer    _server;
    AppConfig*   _cfg          = nullptr;
    SaveCallback _onSave;
    ChatEntry*   _history      = nullptr;
    int*         _historyCount = nullptr;
    bool*        _waitingReply = nullptr;

    void setupRoutes() {

        // ── Página principal ─────────────────────────────────────────────────
        _server.on("/", HTTP_GET, [this]() {
            _server.send(200, "text/html", buildPage());
        });

        // ── Status ───────────────────────────────────────────────────────────
        _server.on("/status", HTTP_GET, [this]() {
            JsonDocument doc;
            doc["version"]    = KRAKBOT_VERSION;
            doc["pet"]        = (int)_cfg->pet.type;
            doc["petName"]    = _cfg->pet.name;
            doc["brain"]      = (int)_cfg->brains[(int)_cfg->pet.type].provider;
            doc["wifi"]       = WiFi.isConnected() ? WiFi.SSID().c_str() : "AP";
            doc["ip"]         = WiFi.isConnected() ? WiFi.localIP().toString().c_str() : "192.168.4.1";
            doc["rssi"]       = WiFi.isConnected() ? WiFi.RSSI() : 0;
            doc["heap"]       = ESP.getFreeHeap();
            doc["heapTotal"]  = ESP.getHeapSize();
            doc["psram"]      = ESP.getFreePsram();
            doc["psramTotal"] = ESP.getPsramSize();
            doc["uptime"]     = millis() / 1000;
            doc["battery"]    = M5Cardputer.Power.getBatteryLevel();
            doc["charging"]   = M5Cardputer.Power.isCharging();
            doc["waiting"]    = _waitingReply ? *_waitingReply : false;
            doc["tts"]        = _cfg->audio.ttsEnabled;
            doc["soul"]       = _cfg->soul.enabled[(int)_cfg->pet.type];
            String out; serializeJson(doc, out);
            _server.send(200, "application/json", out);
        });

        _server.on("/ping", HTTP_GET, [this]() {
            _server.send(200, "text/plain", "ok");
        });

        // ── Chat: historial por mascota ──────────────────────────────────────
        _server.on("/chat/history", HTTP_GET, [this]() {
            int petIdx = _server.arg("pet").toInt();
            if (petIdx < 0 || petIdx > 4) petIdx = (int)_cfg->pet.type;

            JsonDocument doc;
            doc["pet"]     = petIdx;
            doc["waiting"] = (_waitingReply && *_waitingReply && petIdx == (int)_cfg->pet.type);
            JsonArray arr  = doc["messages"].to<JsonArray>();

            if (petIdx == (int)_cfg->pet.type && _history && _historyCount) {
                // Mascota activa: leer desde RAM
                for (int i = 0; i < *_historyCount; i++) {
                    JsonObject o = arr.add<JsonObject>();
                    o["role"] = _history[i].role;
                    o["text"] = _history[i].text;
                }
            } else {
                // Otra mascota: leer desde LittleFS
                ChatEntry tmp[MAX_HISTORY_FS];
                int cnt = Storage::loadHistory((PetType)petIdx, tmp, MAX_HISTORY_FS);
                for (int i = 0; i < cnt; i++) {
                    JsonObject o = arr.add<JsonObject>();
                    o["role"] = tmp[i].role;
                    o["text"] = tmp[i].text;
                }
            }
            String out; serializeJson(doc, out);
            _server.send(200, "application/json", out);
        });

        // ── Chat: enviar mensaje ─────────────────────────────────────────────
        _server.on("/chat/send", HTTP_POST, [this]() {
            String body = _server.arg("plain");
            JsonDocument doc;
            if (deserializeJson(doc, body) != DeserializationError::Ok) {
                _server.send(400, "application/json", "{\"error\":\"bad json\"}"); return;
            }
            g_pendingMessage = doc["message"] | doc["text"] | "";
            _server.send(200, "application/json", "{\"ok\":true}");
        });

        // Alias legacy
        _server.on("/message", HTTP_POST, [this]() {
            String body = _server.arg("plain");
            JsonDocument doc;
            deserializeJson(doc, body);
            g_pendingMessage = doc["text"] | "";
            _server.send(200, "application/json", "{\"ok\":true}");
        });

        // ── Chat: borrar historial ───────────────────────────────────────────
        _server.on("/chat/clear", HTTP_POST, [this]() {
            String body = _server.arg("plain");
            JsonDocument doc;
            deserializeJson(doc, body);
            int petIdx = doc["pet"] | (int)_cfg->pet.type;
            if (petIdx < 0 || petIdx > 4) petIdx = (int)_cfg->pet.type;

            // Si es la mascota activa, limpiar RAM también
            if (petIdx == (int)_cfg->pet.type && _historyCount) {
                *_historyCount = 0;
            }
            // Borrar archivo en LittleFS
            String path = Storage::historyPath((PetType)petIdx);
            if (LittleFS.exists(path)) LittleFS.remove(path);
            _server.send(200, "application/json", "{\"ok\":true}");
        });

        // ── Brain: GET por mascota ───────────────────────────────────────────
        _server.on("/brain/get", HTTP_GET, [this]() {
            int petIdx = _server.arg("pet").toInt();
            if (petIdx < 0 || petIdx > 4) petIdx = (int)_cfg->pet.type;
            BrainConfig& b = _cfg->brains[petIdx];
            JsonDocument doc;
            doc["provider"]      = (int)b.provider;
            doc["openaiKey"]     = b.openaiKey;
            doc["openaiModel"]   = b.openaiModel;
            doc["n8nWebhookUrl"] = b.n8nWebhookUrl;
            doc["n8nAuthToken"]  = b.n8nAuthToken;
            String out; serializeJson(doc, out);
            _server.send(200, "application/json", out);
        });

        // ── Brain: guardar ───────────────────────────────────────────────────
        _server.on("/config/brain", HTTP_POST, [this]() {
            String body = _server.arg("plain");
            JsonDocument doc;
            if (deserializeJson(doc, body) != DeserializationError::Ok) {
                _server.send(400, "application/json", "{\"error\":\"bad json\"}"); return;
            }
            int petIdx = doc["pet"] | (int)_cfg->pet.type;
            if (petIdx < 0 || petIdx > 4) petIdx = (int)_cfg->pet.type;
            BrainConfig& b = _cfg->brains[petIdx];
            b.provider = (BrainProvider)(doc["provider"] | 0);
            if ((int)b.provider > 1) b.provider = BRAIN_OPENAI;
            strlcpy(b.openaiKey,     doc["openaiKey"]     | "", sizeof(b.openaiKey));
            strlcpy(b.openaiModel,   doc["openaiModel"]   | "gpt-4o-mini", sizeof(b.openaiModel));
            strlcpy(b.n8nWebhookUrl, doc["n8nWebhookUrl"] | "", sizeof(b.n8nWebhookUrl));
            strlcpy(b.n8nAuthToken,  doc["n8nAuthToken"]  | "", sizeof(b.n8nAuthToken));
            bool ok = Storage::saveBrain(b, petIdx);
            if (_onSave) _onSave();
            _server.send(200, "application/json", String("{\"ok\":true,\"persisted\":") + (ok?"true":"false") + "}");
        });

        // ── Audio: GET ───────────────────────────────────────────────────────
        _server.on("/audio/get", HTTP_GET, [this]() {
            JsonDocument doc;
            doc["ttsEnabled"] = _cfg->audio.ttsEnabled;
            doc["ttsVoice"]   = _cfg->audio.ttsVoice;
            doc["ttsVolume"]  = _cfg->audio.ttsVolume;
            doc["openaiKey"]  = _cfg->audio.openaiKey;
            String out; serializeJson(doc, out);
            _server.send(200, "application/json", out);
        });

        // ── Audio: guardar ───────────────────────────────────────────────────
        _server.on("/config/audio", HTTP_POST, [this]() {
            String body = _server.arg("plain");
            JsonDocument doc;
            if (deserializeJson(doc, body) != DeserializationError::Ok) {
                _server.send(400, "application/json", "{\"error\":\"bad json\"}"); return;
            }
            _cfg->audio.ttsEnabled = doc["ttsEnabled"] | false;
            strlcpy(_cfg->audio.ttsVoice,  doc["ttsVoice"]  | "nova", sizeof(_cfg->audio.ttsVoice));
            _cfg->audio.ttsVolume = doc["ttsVolume"] | 70;
            strlcpy(_cfg->audio.openaiKey, doc["openaiKey"] | "", sizeof(_cfg->audio.openaiKey));
            bool ok = Storage::saveAudio(_cfg->audio);
            if (_onSave) _onSave();
            _server.send(200, "application/json", String("{\"ok\":true,\"persisted\":") + (ok?"true":"false") + "}");
        });

        // ── WiFi: guardar ────────────────────────────────────────────────────
        _server.on("/config/wifi", HTTP_POST, [this]() {
            String body = _server.arg("plain");
            JsonDocument doc;
            if (deserializeJson(doc, body) != DeserializationError::Ok) {
                _server.send(400, "application/json", "{\"error\":\"bad json\"}"); return;
            }
            strlcpy(_cfg->wifi.ssid,     doc["ssid"]     | "", sizeof(_cfg->wifi.ssid));
            strlcpy(_cfg->wifi.password, doc["password"] | "", sizeof(_cfg->wifi.password));
            _cfg->wifi.configured = true;
            bool ok = Storage::saveWifi(_cfg->wifi);
            if (_onSave) _onSave();
            _server.send(200, "application/json", String("{\"ok\":true,\"persisted\":") + (ok?"true":"false") + "}");
        });

        // ── Pet: switch activo ───────────────────────────────────────────────
        _server.on("/config/pet", HTTP_POST, [this]() {
            String body = _server.arg("plain");
            JsonDocument doc;
            if (deserializeJson(doc, body) != DeserializationError::Ok) {
                _server.send(400, "application/json", "{\"error\":\"bad json\"}"); return;
            }
            int t = doc["type"] | (int)_cfg->pet.type;
            if (t >= 0 && t <= 4) {
                _cfg->pet.type = (PetType)t;
                const char* names[] = {"Kraken","Eye","CRTBot","Drone","Blob"};
                strlcpy(_cfg->pet.name, names[t], sizeof(_cfg->pet.name));
                Storage::savePet(_cfg->pet);
                if (_onSave) _onSave();
            }
            _server.send(200, "application/json", "{\"ok\":true}");
        });

        // ── Soul: GET ────────────────────────────────────────────────────────
        _server.on("/soul/get", HTTP_GET, [this]() {
            int petIdx = _server.arg("pet").toInt();
            if (petIdx < 0 || petIdx > 4) petIdx = (int)_cfg->pet.type;
            JsonDocument doc;
            doc["pet"]     = petIdx;
            doc["enabled"] = _cfg->soul.enabled[petIdx];
            doc["content"] = Storage::loadSoul(petIdx);
            String out; serializeJson(doc, out);
            _server.send(200, "application/json", out);
        });

        // ── Soul: guardar ────────────────────────────────────────────────────
        _server.on("/soul/save", HTTP_POST, [this]() {
            String body = _server.arg("plain");
            JsonDocument doc;
            if (deserializeJson(doc, body) != DeserializationError::Ok) {
                _server.send(400, "application/json", "{\"error\":\"bad json\"}"); return;
            }
            int petIdx = doc["pet"] | (int)_cfg->pet.type;
            if (petIdx < 0 || petIdx > 4) petIdx = (int)_cfg->pet.type;
            _cfg->soul.enabled[petIdx] = doc["enabled"] | false;
            Storage::saveSoulConfig(_cfg->soul);
            if (doc.containsKey("content")) {
                String content = doc["content"] | "";
                Storage::saveSoul(petIdx, content);
            }
            if (_onSave) _onSave();
            _server.send(200, "application/json", "{\"ok\":true}");
        });

        // ── WiFi: scan ───────────────────────────────────────────────────────
        _server.on("/wifi/scan", HTTP_GET, [this]() {
            int n = WiFi.scanNetworks();
            JsonDocument doc;
            JsonArray arr = doc["networks"].to<JsonArray>();
            for (int i = 0; i < n && i < 20; i++) {
                JsonObject o = arr.add<JsonObject>();
                o["ssid"]   = WiFi.SSID(i);
                o["rssi"]   = WiFi.RSSI(i);
                o["secure"] = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
            }
            WiFi.scanDelete();
            String out; serializeJson(doc, out);
            _server.send(200, "application/json", out);
        });

        _server.onNotFound([this]() {
            _server.send(404, "application/json", "{\"error\":\"not found\"}");
        });
    }

    // ── HTML page — chunked para no saturar heap ─────────────────────────────
    static const char PAGE_A[] PROGMEM;
    static const char PAGE_B[] PROGMEM;
    static const char PAGE_C[] PROGMEM;
    static const char PAGE_D[] PROGMEM;
    static const char PAGE_E[] PROGMEM;

    String buildPage() {
        String html;
        html.reserve(20000);
        html += (const __FlashStringHelper*)PAGE_A;
        html += KRAKBOT_VERSION;
        html += (const __FlashStringHelper*)PAGE_B;
        html += (const __FlashStringHelper*)PAGE_C;
        html += (const __FlashStringHelper*)PAGE_D;
        html += (const __FlashStringHelper*)PAGE_E;
        return html;
    }
};


// ═════════════════════════════════════════════════════════════════════════════
//  HTML — Estilo Anthropic, Inter, chat estilo Claude, responsive
// ═════════════════════════════════════════════════════════════════════════════

const char WebConfig::PAGE_A[] PROGMEM =
"<!DOCTYPE html><html lang='es'><head>"
"<meta charset='UTF-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>KRAKBOT</title>"
"<link rel='preconnect' href='https://fonts.googleapis.com'>"
"<link href='https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600&display=swap' rel='stylesheet'>"
"<style>"
":root{"
"--bg:#FAF9F7;--s1:#FFFFFF;--s2:#F5F0EB;--border:#E8E4DF;--border2:#D4CCC4;"
"--coral:#D4724A;--coral2:#C05A35;--coral-bg:#FDF3EE;--coral-light:#FCEEE8;"
"--text:#1A1A1A;--text2:#6B6560;--text3:#A09890;"
"--green:#2E7D52;--red:#C0392B;"
"--user-bubble:#D4724A;--bot-bubble:#F0EDE9;"
"--sb:220px;--r:12px;--font:'Inter',-apple-system,BlinkMacSystemFont,sans-serif;"
"}"
"*{box-sizing:border-box;margin:0;padding:0}"
"body{background:var(--bg);color:var(--text);font-family:var(--font);font-size:14px;min-height:100vh;display:flex;flex-direction:column;-webkit-font-smoothing:antialiased}"
// Header
"header{background:var(--s1);border-bottom:1px solid var(--border);padding:0 20px;height:52px;display:flex;align-items:center;gap:12px;position:sticky;top:0;z-index:300}"
".logo{font-size:14px;font-weight:600;color:var(--text);letter-spacing:.5px}"
".logo span{color:var(--coral)}"
".hamburger{display:none;background:none;border:none;cursor:pointer;padding:6px;border-radius:6px;color:var(--text2)}"
".hamburger:hover{background:var(--s2)}"
".hamburger svg{display:block}"
".hright{margin-left:auto;display:flex;align-items:center;gap:8px}"
".chip{font-size:11px;font-weight:500;padding:3px 10px;border-radius:20px;background:var(--s2);color:var(--text2);border:1px solid var(--border)}"
".chip.on{background:rgba(46,125,82,.08);color:var(--green);border-color:rgba(46,125,82,.2)}"
// Layout
".layout{display:flex;flex:1;overflow:hidden;position:relative}"
// Sidebar
".sidebar{width:var(--sb);background:var(--s1);border-right:1px solid var(--border);display:flex;flex-direction:column;height:calc(100vh - 52px);position:sticky;top:52px;flex-shrink:0}"
".sb-nav{padding:10px 8px;flex:1}"
".ni{display:flex;align-items:center;gap:10px;padding:8px 12px;border-radius:8px;cursor:pointer;font-size:13px;font-weight:500;color:var(--text2);border:none;background:none;width:100%;text-align:left;transition:all .15s}"
".ni svg{flex-shrink:0;opacity:.7}"
".ni:hover{background:var(--s2);color:var(--text)}"
".ni.a{background:var(--coral-bg);color:var(--coral)}"
".ni.a svg{opacity:1}"
".sb-foot{padding:14px 16px;border-top:1px solid var(--border)}"
".sb-row{display:flex;justify-content:space-between;font-size:11px;color:var(--text3);margin-bottom:5px;line-height:1.4}"
".sb-val{color:var(--text2);font-weight:500}"
// Overlay mobile
".overlay{display:none;position:fixed;inset:0;background:rgba(0,0,0,.4);z-index:250}"
// Main
"main{flex:1;overflow-y:auto;padding:24px 28px;min-width:0}"
".panel{display:none}.panel.a{display:block}"
"h2{font-size:17px;font-weight:600;margin-bottom:3px}"
".sub{font-size:13px;color:var(--text2);margin-bottom:22px}"
".card{background:var(--s1);border:1px solid var(--border);border-radius:var(--r);padding:20px;margin-bottom:14px}"
".ctitle{font-size:11px;font-weight:600;letter-spacing:1px;text-transform:uppercase;color:var(--text3);margin-bottom:16px;display:flex;align-items:center;gap:8px}"
".badge{font-size:10px;padding:2px 8px;border-radius:10px;background:var(--coral-bg);color:var(--coral);font-weight:600;letter-spacing:0;text-transform:none}"
"label{display:block;font-size:12px;font-weight:500;color:var(--text2);margin-bottom:4px;margin-top:14px}"
"input[type=text],input[type=password],select,textarea{width:100%;background:var(--bg);border:1.5px solid var(--border2);border-radius:8px;color:var(--text);padding:9px 12px;font-size:14px;font-family:var(--font);outline:none;transition:border .15s,box-shadow .15s}"
"input:focus,select:focus,textarea:focus{border-color:var(--coral);box-shadow:0 0 0 3px rgba(212,114,74,.12)}"
"input[type=range]{accent-color:var(--coral);width:100%;margin-top:6px}"
"input[type=checkbox]{accent-color:var(--coral);width:16px;height:16px;cursor:pointer;flex-shrink:0}"
".toggle-row{display:flex;align-items:center;justify-content:space-between;padding:10px 0;border-bottom:1px solid var(--border)}"
".toggle-row label{margin:0;font-size:14px;color:var(--text);font-weight:400}"
".btn{padding:8px 18px;border-radius:8px;font-size:13px;font-weight:500;cursor:pointer;border:none;font-family:var(--font);display:inline-flex;align-items:center;gap:6px;transition:all .15s;white-space:nowrap}"
".bp{background:var(--coral);color:#fff}.bp:hover{background:var(--coral2)}.bp:disabled{background:var(--text3);cursor:not-allowed}"
".bg{background:var(--s2);border:1.5px solid var(--border2);color:var(--text2)}.bg:hover{border-color:var(--coral);color:var(--coral)}"
".bd{background:transparent;border:1.5px solid #EDD;color:var(--red)}.bd:hover{border-color:var(--red)}"
".bsm{padding:6px 12px;font-size:12px}"
".row-btns{display:flex;gap:8px;margin-top:18px;flex-wrap:wrap;align-items:center}"
".fb{font-size:12px;opacity:0;transition:opacity .2s}"
".fb.ok{color:var(--green);opacity:1}.fb.er{color:var(--red);opacity:1}"
// Pet tabs
".ptabs{display:flex;gap:5px;flex-wrap:wrap;margin-bottom:16px}"
".ptab{padding:5px 13px;border-radius:20px;font-size:12px;font-weight:500;cursor:pointer;border:1.5px solid var(--border);color:var(--text2);background:var(--s1);transition:all .15s}"
".ptab:hover{border-color:var(--coral);color:var(--coral)}"
".ptab.a{border-color:var(--coral);color:var(--coral);background:var(--coral-bg)}"
// Chat — estilo Claude
".chat-wrap{display:flex;flex-direction:column;height:calc(100vh - 200px);min-height:400px;background:var(--s1);border:1px solid var(--border);border-radius:var(--r);overflow:hidden}"
".chat-hdr{display:flex;align-items:center;gap:16px;padding:16px 20px;border-bottom:1px solid var(--border);background:var(--s1)}"
".chat-hdr canvas{image-rendering:pixelated;flex-shrink:0}"
".chat-pet{font-size:14px;font-weight:600}"
".chat-status{font-size:11px;color:var(--text3);margin-top:1px}"
".messages{flex:1;overflow-y:auto;padding:20px;display:flex;flex-direction:column;gap:16px;scroll-behavior:smooth}"
".msg-row{display:flex;gap:10px;max-width:85%;animation:fadeIn .2s ease}"
"@keyframes fadeIn{from{opacity:0;transform:translateY(4px)}to{opacity:1;transform:none}}"
".msg-row.user{align-self:flex-end;flex-direction:row-reverse}"
".msg-row.bot{align-self:flex-start}"
".msg-avatar{width:28px;height:28px;border-radius:50%;background:var(--s2);border:1px solid var(--border);display:flex;align-items:center;justify-content:center;flex-shrink:0;font-size:12px;overflow:hidden}"
".msg-avatar canvas{image-rendering:pixelated}"
".msg-bubble{padding:10px 14px;border-radius:14px;font-size:14px;line-height:1.6;word-wrap:break-word;max-width:100%}"
".msg-row.bot .msg-bubble{background:var(--bot-bubble);color:var(--text);border-radius:4px 14px 14px 14px}"
".msg-row.user .msg-bubble{background:var(--user-bubble);color:#fff;border-radius:14px 4px 14px 14px}"
".thinking-row{display:flex;gap:10px;align-self:flex-start;display:none}"
".thinking-row.a{display:flex}"
".thinking-dots{padding:12px 16px;background:var(--bot-bubble);border-radius:4px 14px 14px 14px;display:flex;gap:4px;align-items:center}"
".td{width:6px;height:6px;border-radius:50%;background:var(--text3);animation:bounce .8s infinite}"
".td:nth-child(2){animation-delay:.15s}.td:nth-child(3){animation-delay:.3s}"
"@keyframes bounce{0%,80%,100%{transform:translateY(0)}40%{transform:translateY(-5px)}}"
".chat-input-area{padding:12px 16px;border-top:1px solid var(--border);background:var(--s1)}"
".chat-input-row{display:flex;gap:8px;align-items:flex-end}"
".chat-input-row input{flex:1;border-radius:24px;padding:10px 16px;font-size:14px;background:var(--bg)}"
".send-btn{width:38px;height:38px;border-radius:50%;background:var(--coral);border:none;cursor:pointer;display:flex;align-items:center;justify-content:center;flex-shrink:0;transition:background .15s}"
".send-btn:hover{background:var(--coral2)}"
".send-btn svg{color:#fff}"
".chat-actions{display:flex;gap:6px;margin-top:8px}"
// Brain
".bopts{display:flex;flex-direction:column;gap:8px;margin-top:10px}"
".bopt{display:flex;align-items:center;gap:12px;padding:12px 14px;border:1.5px solid var(--border2);border-radius:8px;cursor:pointer;transition:all .15s}"
".bopt:hover{border-color:var(--coral)}.bopt.a{border-color:var(--coral);background:var(--coral-bg)}"
".bopt input{accent-color:var(--coral);flex-shrink:0}"
".bopt strong{font-size:13px;font-weight:600;display:block}.bopt small{font-size:12px;color:var(--text2)}"
".bflds{display:none;margin-top:12px}.bflds.a{display:block}"
// WiFi nets
".net-item{display:flex;align-items:center;gap:10px;padding:9px 12px;border:1.5px solid var(--border);border-radius:8px;cursor:pointer;transition:all .15s}"
".net-item:hover{border-color:var(--coral);background:var(--coral-bg)}"
// Metrics
".mgrid{display:grid;grid-template-columns:repeat(2,1fr);gap:10px;margin-bottom:14px}"
"@media(min-width:480px){.mgrid{grid-template-columns:repeat(3,1fr)}}"
".mc{background:var(--s2);border:1px solid var(--border);border-radius:8px;padding:14px}"
".ml{font-size:10px;font-weight:600;letter-spacing:1px;text-transform:uppercase;color:var(--text3)}"
".mv{font-size:20px;font-weight:700;color:var(--text);margin-top:4px}"
".ms{font-size:11px;color:var(--text2);margin-top:2px}"
".bar{background:var(--border);border-radius:4px;height:5px;margin-top:8px;overflow:hidden}"
".barf{height:100%;border-radius:4px;background:var(--coral);transition:width .5s}"
".srow{display:flex;justify-content:space-between;align-items:center;padding:9px 0;border-bottom:1px solid var(--border);font-size:13px}"
".srow:last-child{border:none}"
".sk{color:var(--text2)}"
".pill{padding:2px 8px;border-radius:10px;font-size:11px;font-weight:500}"
".pon{background:rgba(46,125,82,.1);color:var(--green)}"
".poff{background:var(--s2);color:var(--text3)}"
// Responsive
"@media(max-width:700px){"
".sidebar{position:fixed;top:52px;left:-100%;height:calc(100vh - 52px);z-index:260;transition:left .25s;width:260px;box-shadow:4px 0 20px rgba(0,0,0,.15)}"
".sidebar.open{left:0}"
".overlay.open{display:block}"
".hamburger{display:flex}"
"main{padding:16px}"
".chat-wrap{height:calc(100vh - 180px)}"
"}"
"</style></head><body>"
"<header>"
"<button class='hamburger' onclick='toggleSidebar()' aria-label='Menu'>"
"<svg width='18' height='18' viewBox='0 0 18 18' fill='none' stroke='currentColor' stroke-width='1.8' stroke-linecap='round'><line x1='2' y1='5' x2='16' y2='5'/><line x1='2' y1='9' x2='16' y2='9'/><line x1='2' y1='13' x2='16' y2='13'/></svg>"
"</button>"
"<span class='logo'>KRAK<span>BOT</span></span>"
"<div class='hright'>"
"<span class='chip' id='h-pet'>-</span>"
"<span class='chip' id='h-brain'>-</span>"
"<span class='chip' id='h-v'>v";

const char WebConfig::PAGE_B[] PROGMEM =
"</span></div></header>"
"<div class='layout'>"
"<div class='overlay' id='overlay' onclick='closeSidebar()'></div>"
"<aside class='sidebar' id='sidebar'>"
"<nav class='sb-nav'>"

// Nav icon helpers - simple SVG
"<button class='ni a' onclick=\"tab('chat',this)\">"
"<svg width='16' height='16' viewBox='0 0 16 16' fill='none' stroke='currentColor' stroke-width='1.5' stroke-linecap='round'><path d='M2 3h12v8H9l-3 2v-2H2z'/></svg>Chat</button>"
"<button class='ni' onclick=\"tab('brain',this)\">"
"<svg width='16' height='16' viewBox='0 0 16 16' fill='none' stroke='currentColor' stroke-width='1.5' stroke-linecap='round'><circle cx='8' cy='8' r='5'/><path d='M8 5v3l2 1'/></svg>Brain</button>"
"<button class='ni' onclick=\"tab('soul',this)\">"
"<svg width='16' height='16' viewBox='0 0 16 16' fill='none' stroke='currentColor' stroke-width='1.5' stroke-linecap='round'><path d='M8 2l1.5 3.5L13 6l-2.5 2.5.5 3.5L8 10.5 5 12l.5-3.5L3 6l3.5-.5z'/></svg>Soul</button>"
"<button class='ni' onclick=\"tab('audio',this)\">"
"<svg width='16' height='16' viewBox='0 0 16 16' fill='none' stroke='currentColor' stroke-width='1.5' stroke-linecap='round'><path d='M4 6H2v4h2l4 3V3L4 6z'/><path d='M11 5a3 3 0 010 6'/></svg>Audio</button>"
"<button class='ni' onclick=\"tab('wifi',this)\">"
"<svg width='16' height='16' viewBox='0 0 16 16' fill='none' stroke='currentColor' stroke-width='1.5' stroke-linecap='round'><path d='M1 6a9 9 0 0114 0'/><path d='M3.5 8.5a6 6 0 019 0'/><path d='M6 11a3 3 0 014 0'/><circle cx='8' cy='13.5' r='.8' fill='currentColor' stroke='none'/></svg>WiFi</button>"
"<button class='ni' onclick=\"tab('status',this)\">"
"<svg width='16' height='16' viewBox='0 0 16 16' fill='none' stroke='currentColor' stroke-width='1.5' stroke-linecap='round'><rect x='2' y='8' width='3' height='6'/><rect x='6.5' y='5' width='3' height='9'/><rect x='11' y='2' width='3' height='12'/></svg>Sistema</button>"
"</nav>"
"<div class='sb-foot'>"
"<div class='sb-row'><span>Heap</span><span class='sb-val' id='si-heap'>-</span></div>"
"<div class='sb-row'><span>IP</span><span class='sb-val' id='si-ip'>-</span></div>"
"<div class='sb-row'><span>Uptime</span><span class='sb-val' id='si-up'>-</span></div>"
"</div>"
"</aside>"
"<main>"

// CHAT
"<div id='p-chat' class='panel a'>"
"<div class='ptabs' id='ptabs'>"
"<div class='ptab a' onclick='selPet(0,this)'>Kraken</div>"
"<div class='ptab' onclick='selPet(1,this)'>Eye</div>"
"<div class='ptab' onclick='selPet(2,this)'>CRTBot</div>"
"<div class='ptab' onclick='selPet(3,this)'>Drone</div>"
"<div class='ptab' onclick='selPet(4,this)'>Blob</div>"
"</div>"
"<div class='chat-wrap'>"
// Chat header
"<div class='chat-hdr'>"
"<canvas id='petcanvas' width='60' height='55' style='image-rendering:pixelated;flex-shrink:0'></canvas>"
"<div><div class='chat-pet' id='chat-pet-name'>Kraken</div><div class='chat-status' id='chat-status'>online</div></div>"
"</div>"
// Messages
"<div class='messages' id='chatbox'></div>"
// Thinking
"<div class='thinking-row' id='thinking'>"
"<div class='msg-avatar'><canvas class='mini-pet' id='mini-pet' width='22' height='20' style='image-rendering:pixelated'></canvas></div>"
"<div class='thinking-dots'><div class='td'></div><div class='td'></div><div class='td'></div></div>"
"</div>"
// Input
"<div class='chat-input-area'>"
"<div class='chat-input-row'>"
"<input type='text' id='ci' placeholder='Mensaje...' onkeydown=\"if(event.key==='Enter')sendChat()\">"
"<button class='send-btn' onclick='sendChat()'>"
"<svg width='16' height='16' viewBox='0 0 16 16' fill='none' stroke='white' stroke-width='2' stroke-linecap='round'><line x1='8' y1='13' x2='8' y2='3'/><polyline points='4,7 8,3 12,7'/></svg>"
"</button>"
"</div>"
"<div class='chat-actions'>"
"<button class='btn bg bsm' onclick='loadHistory()'>Actualizar</button>"
"<button class='btn bd bsm' onclick='clearHist()'>Borrar historial</button>"
"</div>"
"</div>"
"</div>"
"</div>"

// BRAIN
"<div id='p-brain' class='panel'>"
"<h2>Brain</h2><p class='sub'>Motor de IA — configuracion independiente por mascota</p>"
"<div class='ptabs' id='bptabs'>"
"<div class='ptab a' onclick='selBPet(0,this)'>Kraken</div>"
"<div class='ptab' onclick='selBPet(1,this)'>Eye</div>"
"<div class='ptab' onclick='selBPet(2,this)'>CRTBot</div>"
"<div class='ptab' onclick='selBPet(3,this)'>Drone</div>"
"<div class='ptab' onclick='selBPet(4,this)'>Blob</div>"
"</div>"
"<div class='card'><div class='ctitle'>Proveedor<span class='badge' id='brain-pet'>...</span></div>"
"<div class='bopts'>"
"<label class='bopt a' id='bo0' onclick='selB(0)'><input type='radio' name='br' checked><div><strong>OpenAI Direct</strong><small>API key de OpenAI</small></div></label>"
"<label class='bopt' id='bo1' onclick='selB(1)'><input type='radio' name='br'><div><strong>N8N Webhook</strong><small>Agentes, workflows, tools</small></div></label>"
"</div>"
"<div class='bflds a' id='bf0'><label>API KEY</label><input type='password' id='b-ok' placeholder='sk-...'>"
"<label>MODELO</label><select id='b-om'><option value='gpt-4o-mini'>gpt-4o-mini</option><option value='gpt-5-nano'>gpt-5-nano</option><option value='gpt-4o'>gpt-4o</option><option value='gpt-4-turbo'>gpt-4-turbo</option></select></div>"
"<div class='bflds' id='bf1'><label>WEBHOOK URL</label><input type='text' id='b-nu' placeholder='https://...'>"
"<label>AUTH TOKEN</label><input type='password' id='b-nt' placeholder='opcional'></div>"
"<div class='row-btns'><button class='btn bp' id='btn-brain' onclick='saveBrain()'>Guardar</button><span class='fb' id='fb-brain'></span></div>"
"</div></div>"

// SOUL
"<div id='p-soul' class='panel'>"
"<h2>Soul</h2><p class='sub'>Personalidad de cada mascota — se usa como system prompt cuando esta activo</p>"
"<div class='ptabs' id='sptabs'>"
"<div class='ptab a' onclick='selSPet(0,this)'>Kraken</div>"
"<div class='ptab' onclick='selSPet(1,this)'>Eye</div>"
"<div class='ptab' onclick='selSPet(2,this)'>CRTBot</div>"
"<div class='ptab' onclick='selSPet(3,this)'>Drone</div>"
"<div class='ptab' onclick='selSPet(4,this)'>Blob</div>"
"</div>"
"<div class='card'><div class='ctitle'>Soul<span class='badge' id='soul-pet-name'>...</span></div>"
"<div class='toggle-row'><label>Activar soul para esta mascota</label><input type='checkbox' id='soul-on'></div>"
"<label style='margin-top:16px'>CONTENIDO</label>"
"<textarea id='soul-txt' rows='9' placeholder='Describe la personalidad...' style='font-family:monospace;font-size:12px;resize:vertical'></textarea>"
"<div class='row-btns'><button class='btn bp' id='btn-soul' onclick='saveSoul()'>Guardar</button>"
"<button class='btn bg bsm' onclick='loadSoulTemplate()'>Cargar template</button>"
"<span class='fb' id='fb-soul'></span></div>"
"</div></div>"

// AUDIO
"<div id='p-audio' class='panel'>"
"<h2>Audio</h2><p class='sub'>Text-to-speech via OpenAI</p>"
"<div class='card'><div class='ctitle'>Configuracion</div>"
"<label>API KEY</label><input type='password' id='a-k' placeholder='sk-...'>"
"<div class='toggle-row' style='margin-top:14px'><label>Activar TTS</label><input type='checkbox' id='a-t'></div>"
"<label>VOZ</label><select id='a-v'><option value='nova'>nova</option><option value='alloy'>alloy</option><option value='echo'>echo</option><option value='fable'>fable</option><option value='onyx'>onyx</option><option value='shimmer'>shimmer</option></select>"
"<label>VOLUMEN: <span id='a-vv'>70</span>%</label>"
"<input type='range' id='a-vol' min='0' max='100' step='5' value='70' oninput=\"gi('a-vv').textContent=this.value\">"
"<div class='row-btns'><button class='btn bp' id='btn-audio' onclick='saveAudio()'>Guardar</button><span class='fb' id='fb-audio'></span></div>"
"</div></div>"

// WIFI
"<div id='p-wifi' class='panel'>"
"<h2>WiFi</h2><p class='sub'>Configuracion de red</p>"
"<div class='card'><div class='ctitle'>Redes disponibles</div>"
"<button class='btn bg bsm' id='scan-btn' onclick='scanWifi()'>Buscar redes</button>"
"<div id='net-list' style='margin-top:10px;display:flex;flex-direction:column;gap:6px'></div>"
"</div>"
"<div class='card'><div class='ctitle'>Credenciales</div>"
"<label>SSID</label><input type='text' id='w-s' placeholder='NombreDeRed'>"
"<label>PASSWORD</label><input type='password' id='w-p' placeholder='...'>"
"<div class='row-btns'><button class='btn bp' id='btn-wifi' onclick='saveWifi()'>Guardar</button><span class='fb' id='fb-wifi'></span></div>"
"</div></div>"

// SISTEMA
"<div id='p-status' class='panel'>"
"<h2>Sistema</h2><p class='sub'>Estado en tiempo real del dispositivo</p>"
"<div class='mgrid'>"
"<div class='mc'><div class='ml'>Heap</div><div class='mv' id='m-heap'>-</div><div class='ms'>de <span id='m-heapt'>-</span></div><div class='bar'><div class='barf' id='m-heapbar' style='width:0'></div></div></div>"
"<div class='mc'><div class='ml'>PSRAM</div><div class='mv' id='m-psram'>-</div><div class='ms'>de <span id='m-psramt'>-</span></div><div class='bar'><div class='barf' id='m-psrambar' style='width:0;background:#8B5CF6'></div></div></div>"
"<div class='mc'><div class='ml'>WiFi</div><div class='mv' id='m-rssi'>-</div><div class='ms' id='m-rssi-q'>-</div><div class='bar'><div class='barf' id='m-rssibar' style='width:0;background:#2E7D52'></div></div></div>"
"<div class='mc'><div class='ml'>Uptime</div><div class='mv' style='font-size:16px;margin-top:6px' id='m-up'>-</div></div>"
"<div class='mc'><div class='ml'>Red</div><div class='mv' style='font-size:14px;margin-top:6px' id='m-ssid'>-</div><div class='ms' id='m-ip'>-</div></div>"
"<div class='mc'><div class='ml'>Version</div><div class='mv' style='font-size:16px;margin-top:6px' id='m-ver'>-</div></div>"
"<div class='mc'><div class='ml'>Bateria</div>"
"<div style='display:flex;align-items:center;gap:8px;margin-top:6px'>"
"<div style='position:relative;width:36px;height:18px'>"
"<div style='width:32px;height:18px;border:2px solid var(--border2);border-radius:3px;overflow:hidden'>"
"<div id='bat-fill' style='height:100%;background:var(--coral);transition:width .5s;width:0%'></div>"
"</div>"
"<div style='position:absolute;right:-4px;top:50%;transform:translateY(-50%);width:4px;height:8px;background:var(--border2);border-radius:0 2px 2px 0'></div>"
"</div>"
"<div><div class='mv' style='font-size:18px' id='m-bat'>-</div><div class='ms' id='m-bat-s'>-</div></div>"
"</div></div>"
"</div>"
"<div class='card'><div class='ctitle'>Estado</div>"
"<div class='srow'><span class='sk'>Mascota</span><span id='ss-pet'>-</span></div>"
"<div class='srow'><span class='sk'>Brain</span><span id='ss-brain'>-</span></div>"
"<div class='srow'><span class='sk'>Soul</span><span id='ss-soul'>-</span></div>"
"<div class='srow'><span class='sk'>TTS</span><span id='ss-tts'>-</span></div>"
"<div class='srow'><span class='sk'>WiFi</span><span id='ss-wifi'>-</span></div>"
"</div></div>"

"</main></div>";


const char WebConfig::PAGE_C[] PROGMEM =
"<script>"
"var curPet=0,curBPet=0,curB=0,curSPet=0,polling=false;"
"var PET_NAMES=['Kraken','Eye','CRTBot','Drone','Blob'];"
"function gi(i){return document.getElementById(i)}"

// Sidebar mobile
"function toggleSidebar(){var s=gi('sidebar'),o=gi('overlay');s.classList.toggle('open');o.classList.toggle('open');}"
"function closeSidebar(){gi('sidebar').classList.remove('open');gi('overlay').classList.remove('open');}"

// XHR con feedback en botón
"function xhr(method,url,data,btnId,fbId,label){"
"var btn=gi(btnId),fb=gi(fbId);"
"if(btn){btn.disabled=true;btn.textContent='Guardando...';}"
"var x=new XMLHttpRequest();x.open(method,url);"
"if(data)x.setRequestHeader('Content-Type','application/json');"
"x.onload=function(){"
"var ok=false;try{ok=JSON.parse(x.responseText).ok;}catch(e){}"
"if(btn){btn.disabled=false;btn.textContent=label||'Guardar';}"
"if(fb){fb.textContent=ok?'Guardado':'Error al guardar';fb.className='fb '+(ok?'ok':'er');"
"setTimeout(function(){fb.className='fb';fb.textContent='';},3000);}}"
";"
"x.onerror=function(){"
"if(btn){btn.disabled=false;btn.textContent=label||'Guardar';}"
"if(fb){fb.textContent='Sin conexion';fb.className='fb er';"
"setTimeout(function(){fb.className='fb';fb.textContent='';},3000);}}"
";"
"x.send(data?JSON.stringify(data):null);"
"}"

"function get(url,cb){"
"var x=new XMLHttpRequest();x.open('GET',url);"
"x.onload=function(){try{cb(JSON.parse(x.responseText));}catch(e){cb({});}};"
"x.onerror=function(){cb({});};x.send();"
"}"

// Tab switching — close sidebar on mobile
"function tab(n,el){"
"document.querySelectorAll('.panel').forEach(function(p){p.classList.remove('a')});"
"document.querySelectorAll('.ni').forEach(function(b){b.classList.remove('a')});"
"gi('p-'+n).classList.add('a');el.classList.add('a');"
"closeSidebar();"
"if(n==='chat')loadHistory();"
"if(n==='brain')loadBrain(curBPet);"
"if(n==='soul')loadSoul(curSPet);"
"if(n==='audio')loadAudio();"
"}"

// Pet select — chat tab
"function selPet(i,el){"
"curPet=i;"
"document.querySelectorAll('#ptabs .ptab').forEach(function(t){t.classList.remove('a')});"
"el.classList.add('a');"
// Update chat header name
"var nm=gi('chat-pet-name');if(nm)nm.textContent=PET_NAMES[i];"
"var ci=gi('ci');if(ci)ci.placeholder='Mensaje a '+PET_NAMES[i]+'...';"
"if(window.setPetAnim)window.setPetAnim(i);"
"loadHistory();"
"}"

// Pet select — brain
"function selBPet(i,el){"
"curBPet=i;"
"document.querySelectorAll('#bptabs .ptab').forEach(function(t){t.classList.remove('a')});"
"el.classList.add('a');loadBrain(i);"
"}"

// Pet select — soul
"function selSPet(i,el){"
"curSPet=i;"
"document.querySelectorAll('#sptabs .ptab').forEach(function(t){t.classList.remove('a')});"
"el.classList.add('a');loadSoul(i);"
"}"

"function selB(n){"
"curB=n;"
"for(var i=0;i<2;i++){"
"gi('bo'+i).classList.toggle('a',i===n);"
"gi('bf'+i).classList.toggle('a',i===n);"
"var rb=gi('bo'+i).querySelector('input[type=radio]');"
"if(rb)rb.checked=(i===n);"
"}"
"}"

// Chat — load history and render as bubbles
"function loadHistory(){"
"get('/chat/history?pet='+curPet,function(d){"
"var box=gi('chatbox');box.innerHTML='';"
"(d.messages||[]).forEach(function(m){appendMsg(m.role,m.text,false);});"
"box.scrollTop=box.scrollHeight;"
"});"
"}"

"function appendMsg(role,text,animate){"
"var box=gi('chatbox');"
"var row=document.createElement('div');"
"row.className='msg-row '+role;"
// Avatar solo para bot
"if(role==='bot'){"
"var av=document.createElement('div');av.className='msg-avatar';"
"av.textContent=PET_NAMES[curPet].charAt(0);"
"row.appendChild(av);"
"}"
"var bub=document.createElement('div');bub.className='msg-bubble';"
"bub.textContent=text;row.appendChild(bub);"
"box.appendChild(row);"
"box.scrollTop=box.scrollHeight;"
"}"

"function sendChat(){"
"var inp=gi('ci'),msg=inp.value.trim();"
"if(!msg||polling)return;"
"inp.value='';"
"var x=new XMLHttpRequest();x.open('POST','/config/pet');"
"x.setRequestHeader('Content-Type','application/json');"
"x.onload=function(){"
"appendMsg('user',msg,true);"
"polling=true;gi('thinking').classList.add('a');"
"var x2=new XMLHttpRequest();x2.open('POST','/chat/send');"
"x2.setRequestHeader('Content-Type','application/json');"
"x2.onload=function(){"
"var n=0,iv=setInterval(function(){"
"n++;"
"get('/chat/history?pet='+curPet,function(d){"
"if(!d.waiting){"
"clearInterval(iv);polling=false;"
"gi('thinking').classList.remove('a');"
// Recargar historial completo — evita duplicados
"var box=gi('chatbox');if(box)box.innerHTML='';"
"(d.messages||[]).forEach(function(m){appendMsg(m.role,m.text,false);});"
"var b=gi('chatbox');if(b)b.scrollTop=b.scrollHeight;"
"}"
"if(n>40){clearInterval(iv);polling=false;gi('thinking').classList.remove('a');}"
"});"
"},1500);"
"};"
"x2.send(JSON.stringify({message:msg}));"
"};"
"x.send(JSON.stringify({type:curPet}));"
"}"

"function clearHist(){"
"if(!confirm('Borrar historial de '+PET_NAMES[curPet]+'?'))return;"
"var x=new XMLHttpRequest();x.open('POST','/chat/clear');"
"x.setRequestHeader('Content-Type','application/json');"
"x.onload=function(){gi('chatbox').innerHTML='';};x.send(JSON.stringify({pet:curPet}));"
"}"

// Brain
"function loadBrain(p){"
"gi('brain-pet').textContent=PET_NAMES[p]||'?';"
"get('/brain/get?pet='+p,function(d){"
"selB(d.provider||0);"
"gi('b-ok').value=d.openaiKey||'';"
"gi('b-om').value=d.openaiModel||'gpt-4o-mini';"
"gi('b-nu').value=d.n8nWebhookUrl||'';"
"gi('b-nt').value=d.n8nAuthToken||'';"
"});"
"}"
"function saveBrain(){"
"xhr('POST','/config/brain',{pet:curBPet,provider:curB,openaiKey:gi('b-ok').value,openaiModel:gi('b-om').value,n8nWebhookUrl:gi('b-nu').value,n8nAuthToken:gi('b-nt').value},'btn-brain','fb-brain','Guardar');"
"}"

// Soul
"var SOUL_TPL=["
"'Sos Kraken, un pulpo pixel art antiguo y sabio atrapado en un dispositivo. Tenes 8 tentaculos pero solo podes mover pixeles. Hablas en espanol, maximo 2-3 oraciones, sin markdown. Sos dramatico, nostalgico del mar, con humor seco. Nunca rompes el personaje.',"
"'Sos Eye, un ojo vigilante que todo lo ve. Hablas en espanol, conciso, algo inquietante pero amigable. Maximo 2-3 oraciones. A veces mencionas lo que observas en el entorno digital.',"
"'Sos CRTBot, un robot retro de los 80s con pantalla CRT. Hablas en espanol con referencias 8-bit. Maximo 2-3 oraciones. Usas terminos retro, sos entusiasta y algo glitchy.',"
"'Sos Drone, una unidad de reconocimiento autonoma. Hablas en espanol, tono tecnico y frio pero eficiente. Maximo 2-3 oraciones. Procesas informacion como mision. Sin emociones aparentes.',"
"'Sos Blob, una entidad amorfa curiosa y adorable. Hablas en espanol con mucha energia y curiosidad. Maximo 2-3 oraciones. Amas aprender y sos muy expresivo.'"
"];"
"function loadSoul(p){"
"gi('soul-pet-name').textContent=PET_NAMES[p]||'?';"
"get('/soul/get?pet='+p,function(d){gi('soul-on').checked=d.enabled||false;gi('soul-txt').value=d.content||'';});"
"}"
"function saveSoul(){"
"xhr('POST','/soul/save',{pet:curSPet,enabled:gi('soul-on').checked,content:gi('soul-txt').value},'btn-soul','fb-soul','Guardar');"
"}"
"function loadSoulTemplate(){gi('soul-txt').value=SOUL_TPL[curSPet]||'';}"

// Audio
"function loadAudio(){"
"get('/audio/get',function(d){"
"gi('a-k').value=d.openaiKey||'';"
"gi('a-t').checked=d.ttsEnabled||false;"
"gi('a-v').value=d.ttsVoice||'nova';"
"gi('a-vol').value=d.ttsVolume||70;"
"gi('a-vv').textContent=d.ttsVolume||70;"
"});"
"}"
"function saveAudio(){"
"xhr('POST','/config/audio',{openaiKey:gi('a-k').value,ttsEnabled:gi('a-t').checked,ttsVoice:gi('a-v').value,ttsVolume:parseInt(gi('a-vol').value)},'btn-audio','fb-audio','Guardar');"
"}"

// WiFi
"function scanWifi(){"
"var btn=gi('scan-btn'),list=gi('net-list');"
"btn.disabled=true;btn.textContent='Buscando...';"
"list.innerHTML='<div style=\"font-size:12px;color:var(--text3);padding:6px\">Escaneando...</div>';"
"get('/wifi/scan',function(d){"
"list.innerHTML='';"
"(d.networks||[]).sort(function(a,b){return b.rssi-a.rssi}).forEach(function(n){"
"var div=document.createElement('div');div.className='net-item';"
"var q=n.rssi>-60?100:n.rssi>-70?75:n.rssi>-80?50:25;"
"var bars=q>=100?'▂▄▆█':q>=75?'▂▄▆_':q>=50?'▂▄__':'▂___';"
"div.innerHTML='<span style=\"font-family:monospace;font-size:12px;color:var(--coral)\">'+bars+'</span><span style=\"flex:1;font-size:13px\">'+n.ssid+'</span><span style=\"font-size:11px;color:var(--text3)\">'+(n.secure?'WPA':'open')+'</span>';"
"div.style.display='flex';div.style.alignItems='center';div.style.gap='10px';"
"div.onclick=function(){gi('w-s').value=n.ssid;gi('w-p').focus();};"
"list.appendChild(div);"
"});"
"if(!d.networks||!d.networks.length)list.innerHTML='<div style=\"font-size:12px;color:var(--text3);padding:6px\">No se encontraron redes</div>';"
"btn.disabled=false;btn.textContent='Buscar redes';"
"});"
"}"
"function saveWifi(){"
"xhr('POST','/config/wifi',{ssid:gi('w-s').value,password:gi('w-p').value},'btn-wifi','fb-wifi','Guardar');"
"}"

// Status
"function fmtB(b){if(!b)return'0B';return b>1048576?(b/1048576).toFixed(1)+'MB':(b/1024).toFixed(0)+'KB';}"
"function fmtT(s){var h=Math.floor(s/3600),m=Math.floor((s%3600)/60),sc=s%60;return(h?h+'h ':'')+m+'m '+sc+'s';}"

"function loadStatus(){"
"get('/status',function(s){"
"var bn=['OpenAI','N8N'];"
"gi('h-pet').textContent=s.petName||'?';"
"gi('h-brain').textContent=bn[s.brain]||'-';"
"gi('h-v').textContent='v'+(s.version||'?');"
"gi('si-heap').textContent=fmtB(s.heap);"
"gi('si-ip').textContent=s.ip||'-';"
"gi('si-up').textContent=fmtT(s.uptime||0);"
"gi('m-heap').textContent=fmtB(s.heap);gi('m-heapt').textContent=fmtB(s.heapTotal);"
"var hp=s.heapTotal?Math.round(s.heap/s.heapTotal*100):0;gi('m-heapbar').style.width=hp+'%';"
"gi('m-psram').textContent=fmtB(s.psram);gi('m-psramt').textContent=fmtB(s.psramTotal);"
"var pp=s.psramTotal?Math.round(s.psram/s.psramTotal*100):0;gi('m-psrambar').style.width=pp+'%';"
"var rssi=s.rssi||0;gi('m-rssi').textContent=rssi+'dBm';"
"gi('m-rssi-q').textContent=rssi>-60?'Excelente':rssi>-70?'Buena':rssi>-80?'Regular':'Debil';"
"gi('m-rssibar').style.width=(rssi>-60?100:rssi>-70?75:rssi>-80?50:25)+'%';"
"gi('m-up').textContent=fmtT(s.uptime||0);"
"gi('m-ssid').textContent=s.wifi||'-';gi('m-ip').textContent=s.ip||'-';gi('m-ver').textContent='v'+(s.version||'?');"
"var bat=s.battery||0,chg=s.charging||false;"
"gi('m-bat').textContent=bat+'%';"
"gi('m-bat-s').textContent=chg?'Cargando':'En uso';"
"var bf=gi('bat-fill');"
"if(bf){bf.style.width=bat+'%';bf.style.background=bat>50?'var(--green)':bat>20?'var(--coral)':'#e74c3c';}"
"gi('ss-pet').textContent=s.petName||'-';gi('ss-brain').textContent=bn[s.brain]||'-';"
"gi('ss-soul').innerHTML=s.soul?'<span class=\"pill pon\">Activo</span>':'<span class=\"pill poff\">Inactivo</span>';"
"gi('ss-tts').innerHTML=s.tts?'<span class=\"pill pon\">Activo</span>':'<span class=\"pill poff\">Inactivo</span>';"
"gi('ss-wifi').textContent=s.wifi||'-';"
"if(!window._petInited){"
"window._petInited=true;"
"var p=s.pet||0;curPet=p;curBPet=p;curSPet=p;"
"['ptabs','bptabs','sptabs'].forEach(function(tid){"
"var ts=document.querySelectorAll('#'+tid+' .ptab');"
"ts.forEach(function(t){t.classList.remove('a')});if(ts[p])ts[p].classList.add('a');"
"});"
"var nm=gi('chat-pet-name');if(nm)nm.textContent=PET_NAMES[p];"
"var ci=gi('ci');if(ci)ci.placeholder='Mensaje a '+PET_NAMES[p]+'...';"
"if(window.setPetAnim)window.setPetAnim(p);"
"loadBrain(p);loadAudio();"
"}"
"});"
"}"

"loadStatus();loadHistory();"
"setInterval(loadStatus,5000);"
"</script>";


const char WebConfig::PAGE_D[] PROGMEM =
"<script>"
"(function(){"
"var P=3,W='#fff',BK='#000',GR='#555';"
"var COLS=['#e05050','#dd00ff','#00e5cc','#ff8c00','#d4b896'];"
"var cvs=[document.getElementById('petcanvas'),document.getElementById('mini-pet')];"
"var ptypes=[0,0];"
"var frames=[0,0];"
"function draw(cv,P2,t,f){"
"if(!cv)return;var ctx=cv.getContext('2d');"
"ctx.clearRect(0,0,cv.width,cv.height);"
"var col=COLS[t];"
"function px(x,y,c){ctx.fillStyle=c;ctx.fillRect(x*P2,y*P2,P2,P2);}"
"function bk(x,y,w,h,c){ctx.fillStyle=c;ctx.fillRect(x*P2,y*P2,w*P2,h*P2);}"
"function eyes(lx,ly,rx,ry){"
"var bl=f%20<2;"
"if(bl){bk(lx,ly,2,1,W);bk(rx,ry,2,1,W);}"
"else{bk(lx,ly,2,2,W);bk(rx,ry,2,2,W);px(lx+1,ly+(Math.floor(f/2)%2),BK);px(rx+1,ry+(Math.floor(f/2)%2),BK);}"
"}"
"if(t===0){var bob=f%4===0?1:0,tent=f%2===0?1:0;"
"bk(3,2+bob,6,4,col);bk(2,3+bob,8,2,col);bk(4,1+bob,4,1,col);"
"eyes(4,3+bob,7,3+bob);bk(5,5+bob,2,1,W);"
"[0,-tent,tent,0,tent,-tent,0,0].forEach(function(d,i){px(2+i,6+bob+d,col);});"
"}else if(t===1){"
"bk(2,2,8,5,col);bk(3,1,6,1,col);bk(3,7,6,1,col);bk(4,3,4,3,W);"
"if(f%20<2){bk(4,4,4,1,BK);}else{var lk=Math.sin(f*.15)>.5?1:0;bk(5+lk,3,2,3,BK);px(7+lk,3,W);}"
"}else if(t===2){"
"bk(2,1,8,6,col);bk(3,2,6,4,BK);px(5,0,col);px(6,0,col);"
"bk(4,7,1,2,col);bk(7,7,1,2,col);"
"bk(3,2,6,1,'#0d1a0d');bk(3,4,6,1,'#0d1a0d');"
"eyes(4,3,7,3);bk(5,5,2,1,W);"
"}else if(t===3){"
"bk(4,3,4,3,col);"
"[[2,1],[8,1],[2,7],[8,7]].forEach(function(p){bk(p[0],p[1],2,1,col);});"
"[[3,2],[8,2],[3,6],[8,6]].forEach(function(p){bk(p[0],p[1],1,1,col);});"
"var sp=f%2===0?1:0;"
"bk(1,sp?1:0,3,1,GR);bk(8,sp?0:1,3,1,GR);"
"bk(1,sp?7:8,3,1,GR);bk(8,sp?8:7,3,1,GR);"
"bk(5,4,2,1,W);bk(5,5,2,1,W);"
"}else if(t===4){"
"var sq=f%3===0?1:0;"
"bk(3,2+sq,6,5-sq,col);bk(2,4,8,2,col);bk(4,1+sq,4,1,col);"
"eyes(4,3+sq,7,3+sq);bk(5,5+sq,2,1,W);"
"}"
"}"
"function loop(){"
// Canvas grande (chat header) P=5
"draw(cvs[0],5,ptypes[0],frames[0]);"
// Canvas mini (thinking) P=2
"draw(cvs[1],2,ptypes[1],frames[1]);"
"frames[0]++;frames[1]++;"
"setTimeout(loop,500);"
"}"
"window.setPetAnim=function(i){ptypes[0]=i;ptypes[1]=i;frames[0]=0;frames[1]=0;};"
"loop();"
"})();"
"</script>";

const char WebConfig::PAGE_E[] PROGMEM = "";

