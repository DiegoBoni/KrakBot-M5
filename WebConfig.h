#pragma once
#include <WebServer.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include "config.h"
#include "Storage.h"

// ─────────────────────────────────────────────
// WebConfig — panel de configuración en port 80
// Usa WebServer nativo del ESP32 (sin libs extra)
// ─────────────────────────────────────────────

class WebConfig {
public:
    WebConfig() : _server(80) {}

    using SaveCallback = std::function<void()>;
    void onSave(SaveCallback cb) { _onSave = cb; }

    void begin(AppConfig& cfg) {
        _cfg = &cfg;
        setupRoutes();
        _server.begin();
        Serial.println("[Web] Server started on port 80");
    }

    // Llamar en el loop principal
    void handle() {
        _server.handleClient();
    }

private:
    WebServer    _server;
    AppConfig*   _cfg    = nullptr;
    SaveCallback _onSave;

    void setupRoutes() {
        _server.on("/", HTTP_GET, [this]() {
            _server.send(200, "text/html", buildPage());
        });

        _server.on("/status", HTTP_GET, [this]() {
            JsonDocument doc;
            doc["version"] = KRAKBOT_VERSION;
            doc["pet"]     = (int)_cfg->pet.type;
            doc["petName"] = _cfg->pet.name;
            doc["brain"]   = (int)_cfg->brain.provider;
            doc["wifi"]    = WiFi.isConnected() ? WiFi.SSID().c_str() : "AP";
            doc["heap"]    = ESP.getFreeHeap();
            String out;
            serializeJson(doc, out);
            _server.send(200, "application/json", out);
        });

        _server.on("/config/wifi", HTTP_POST, [this]() {
            String body = _server.arg("plain");
            JsonDocument doc;
            if (deserializeJson(doc, body) != DeserializationError::Ok) {
                _server.send(400, "application/json", "{\"error\":\"bad json\"}");
                return;
            }
            strlcpy(_cfg->wifi.ssid,     doc["ssid"]     | "", sizeof(_cfg->wifi.ssid));
            strlcpy(_cfg->wifi.password, doc["password"] | "", sizeof(_cfg->wifi.password));
            _cfg->wifi.configured = true;
            bool persisted = Storage::saveWifi(_cfg->wifi);
            if (_onSave) _onSave();
            _server.send(200, "application/json",
                         String("{\"ok\":true,\"persisted\":") + (persisted ? "true" : "false") + "}");
        });

        _server.on("/config/brain", HTTP_POST, [this]() {
            String body = _server.arg("plain");
            JsonDocument doc;
            if (deserializeJson(doc, body) != DeserializationError::Ok) {
                _server.send(400, "application/json", "{\"error\":\"bad json\"}");
                return;
            }
            _cfg->brain.provider = (BrainProvider)(doc["provider"] | 0);
            strlcpy(_cfg->brain.openaiKey,        doc["openaiKey"]        | "", sizeof(_cfg->brain.openaiKey));
            strlcpy(_cfg->brain.openaiModel,      doc["openaiModel"]      | "gpt-4o-mini", sizeof(_cfg->brain.openaiModel));
            strlcpy(_cfg->brain.n8nWebhookUrl,    doc["n8nWebhookUrl"]    | "", sizeof(_cfg->brain.n8nWebhookUrl));
            strlcpy(_cfg->brain.n8nAuthToken,     doc["n8nAuthToken"]     | "", sizeof(_cfg->brain.n8nAuthToken));
            strlcpy(_cfg->brain.claudeGatewayUrl, doc["claudeGatewayUrl"] | "", sizeof(_cfg->brain.claudeGatewayUrl));
            strlcpy(_cfg->brain.claudeAuthToken,  doc["claudeAuthToken"]  | "", sizeof(_cfg->brain.claudeAuthToken));
            bool persisted = Storage::saveBrain(_cfg->brain);
            if (_onSave) _onSave();
            _server.send(200, "application/json",
                         String("{\"ok\":true,\"persisted\":") + (persisted ? "true" : "false") + "}");
        });

        _server.on("/config/audio", HTTP_POST, [this]() {
            String body = _server.arg("plain");
            JsonDocument doc;
            if (deserializeJson(doc, body) != DeserializationError::Ok) {
                _server.send(400, "application/json", "{\"error\":\"bad json\"}");
                return;
            }
            _cfg->audio.whisperEnabled = doc["whisperEnabled"] | false;
            _cfg->audio.ttsEnabled     = doc["ttsEnabled"]     | false;
            strlcpy(_cfg->audio.ttsVoice,  doc["ttsVoice"]  | "nova", sizeof(_cfg->audio.ttsVoice));
            _cfg->audio.ttsSpeed  = doc["ttsSpeed"]  | 1.0f;
            _cfg->audio.ttsVolume = doc["ttsVolume"] | 80;
            strlcpy(_cfg->audio.openaiKey, doc["openaiKey"] | "", sizeof(_cfg->audio.openaiKey));
            bool persisted = Storage::saveAudio(_cfg->audio);
            if (_onSave) _onSave();
            _server.send(200, "application/json",
                         String("{\"ok\":true,\"persisted\":") + (persisted ? "true" : "false") + "}");
        });

        _server.on("/config/pet", HTTP_POST, [this]() {
            String body = _server.arg("plain");
            JsonDocument doc;
            if (deserializeJson(doc, body) != DeserializationError::Ok) {
                _server.send(400, "application/json", "{\"error\":\"bad json\"}");
                return;
            }
            _cfg->pet.type = (PetType)(doc["type"] | 0);
            strlcpy(_cfg->pet.name, doc["name"] | "Kraken", sizeof(_cfg->pet.name));
            bool persisted = Storage::savePet(_cfg->pet);
            if (_onSave) _onSave();
            _server.send(200, "application/json",
                         String("{\"ok\":true,\"persisted\":") + (persisted ? "true" : "false") + "}");
        });

        _server.on("/message", HTTP_POST, [this]() {
            String body = _server.arg("plain");
            JsonDocument doc;
            if (deserializeJson(doc, body) != DeserializationError::Ok) {
                _server.send(400, "application/json", "{\"error\":\"bad json\"}");
                return;
            }
            g_pendingMessage = doc["text"] | "";
            _server.send(200, "application/json", "{\"ok\":true}");
        });

        _server.onNotFound([this]() {
            _server.send(404, "application/json", "{\"error\":\"not found\"}");
        });
    }

    // HTML en PROGMEM partido en chunks para no tocar el stack
    // Sin F() — el macro F() no acepta strings con comas en el contenido
    static const char PAGE_1[] PROGMEM;
    static const char PAGE_2[] PROGMEM;
    static const char PAGE_3[] PROGMEM;

    String buildPage() {
        String html;
        html.reserve(7000);
        html += (const __FlashStringHelper*)PAGE_1;
        html += KRAKBOT_VERSION;
        html += (const __FlashStringHelper*)PAGE_2;
        html += (const __FlashStringHelper*)PAGE_3;
        return html;
    }
};

// ── Chunks HTML en flash (PROGMEM) ─────────────────────────────────────────
// Partidos para no superar el límite de string literal del compilador.
// Sin uso de F() para evitar conflicto con el macro de Arduino.

const char WebConfig::PAGE_1[] PROGMEM =
"<!DOCTYPE html>"
"<html lang='es'>"
"<head>"
"<meta charset='UTF-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>KRAKBOT Config</title>"
"<style>"
":root{--bg:#0a0a0f;--card:#12121a;--border:#1e1e2e;--accent:#00ffff;--text:#e0e0f0;--muted:#666;--ok:#00ff88;--err:#ff4060}"
"*{box-sizing:border-box;margin:0;padding:0}"
"body{background:var(--bg);color:var(--text);font-family:'Courier New',monospace;padding:16px}"
"h1{color:var(--accent);letter-spacing:3px;font-size:1.3rem;margin-bottom:4px}"
".sub{color:var(--muted);font-size:.75rem;margin-bottom:20px}"
".sbar{display:flex;gap:16px;flex-wrap:wrap;font-size:.75rem;color:var(--muted);margin-bottom:20px;padding:10px;border:1px solid var(--border)}"
".sbar span{color:var(--accent)}"
".tabs{display:flex;gap:4px;margin-bottom:16px;flex-wrap:wrap}"
".tab{padding:8px 14px;background:var(--card);border:1px solid var(--border);color:var(--muted);cursor:pointer;font-family:inherit;font-size:.8rem;letter-spacing:1px}"
".tab.a,.tab:hover{border-color:var(--accent);color:var(--accent)}"
".panel{display:none}.panel.a{display:block}"
".card{background:var(--card);border:1px solid var(--border);padding:16px;margin-bottom:12px}"
".card h2{font-size:.8rem;letter-spacing:2px;color:var(--accent);margin-bottom:14px}"
"label{display:block;font-size:.72rem;color:var(--muted);margin-top:10px;margin-bottom:3px;letter-spacing:1px}"
"input[type=text],input[type=password],input[type=number],select{width:100%;background:#0a0a0f;border:1px solid var(--border);color:var(--text);padding:9px 11px;font-family:inherit;font-size:.88rem;outline:none}"
"input:focus,select:focus{border-color:var(--accent)}"
"input[type=range]{width:100%;accent-color:var(--accent);margin-top:6px}"
".tr{display:flex;justify-content:space-between;align-items:center;margin-top:10px}"
".tr label{margin:0}"
"input[type=checkbox]{accent-color:var(--accent);width:18px;height:18px}"
"button{margin-top:16px;padding:11px 20px;background:transparent;border:1px solid var(--accent);color:var(--accent);font-family:inherit;font-size:.82rem;letter-spacing:2px;cursor:pointer;width:100%}"
"button:hover{background:var(--accent);color:var(--bg)}"
".pgrid{display:grid;grid-template-columns:repeat(5,1fr);gap:6px;margin-top:10px}"
".popt{background:var(--bg);border:1px solid var(--border);padding:10px 2px;text-align:center;cursor:pointer;font-size:.68rem;color:var(--muted)}"
".popt:hover,.popt.s{border-color:var(--accent);color:var(--accent)}"
".popt em{font-size:1.4rem;display:block;margin-bottom:2px;font-style:normal}"
".bopts{display:flex;flex-direction:column;gap:6px;margin-top:10px}"
".bopt{display:flex;align-items:center;gap:10px;padding:10px;background:var(--bg);border:1px solid var(--border);cursor:pointer}"
".bopt:hover,.bopt.s{border-color:var(--accent)}"
".bopt input{accent-color:var(--accent)}"
".bflds{display:none;margin-top:12px}.bflds.a{display:block}"
".msg{padding:9px;font-size:.78rem;margin-top:6px;display:none}"
".msg.ok{color:var(--ok);border:1px solid var(--ok)}"
".msg.er{color:var(--err);border:1px solid var(--err)}"
"</style></head><body>"
"<h1>KRAKBOT</h1>"
"<div class='sub'>COMPANION OS v";

const char WebConfig::PAGE_2[] PROGMEM =
" - CONFIG PANEL</div>"
"<div class='sbar'>"
"<div>WIFI: <span id='sw'>...</span></div>"
"<div>BRAIN: <span id='sb'>-</span></div>"
"<div>PET: <span id='sp'>-</span></div>"
"<div>HEAP: <span id='sh'>-</span></div>"
"</div>"
"<div class='tabs'>"
"<button class='tab a' onclick=\"tab('wifi',this)\">WiFi</button>"
"<button class='tab'   onclick=\"tab('brain',this)\">Brain</button>"
"<button class='tab'   onclick=\"tab('audio',this)\">Audio</button>"
"<button class='tab'   onclick=\"tab('pet',this)\">Pet</button>"
"</div>"
"<div id='p-wifi' class='panel a'>"
"<div class='card'><h2>WI-FI</h2>"
"<label>SSID</label><input type='text' id='w-s' placeholder='MiRed'>"
"<label>PASSWORD</label><input type='password' id='w-p' placeholder='...'>"
"<button onclick=\"save('wifi',{ssid:document.getElementById('w-s').value,password:document.getElementById('w-p').value})\">GUARDAR</button>"
"<div class='msg' id='m-wifi'></div>"
"</div></div>"
"<div id='p-brain' class='panel'>"
"<div class='card'><h2>BRAIN PROVIDER</h2>"
"<div class='bopts'>"
"<label class='bopt s' id='bo0'><input type='radio' name='br' checked onchange='selB(0)'><div><strong>OpenAI Direct</strong><div style='font-size:.72rem;color:var(--muted)'>Standalone</div></div></label>"
"<label class='bopt'   id='bo1'><input type='radio' name='br'       onchange='selB(1)'><div><strong>N8N Webhook</strong><div style='font-size:.72rem;color:var(--muted)'>Agentes / workflows</div></div></label>"
"<label class='bopt'   id='bo2'><input type='radio' name='br'       onchange='selB(2)'><div><strong>Claude Gateway</strong><div style='font-size:.72rem;color:var(--muted)'>Dev companion</div></div></label>"
"</div>"
"<div class='bflds a' id='bf0'>"
"<label>API KEY</label><input type='password' id='b-ok' placeholder='sk-...'>"
"<label>MODELO</label>"
"<select id='b-om'>"
"<option value='gpt-4o-mini'>gpt-4o-mini (recomendado)</option>"
"<option value='gpt-4o'>gpt-4o</option>"
"<option value='gpt-4-turbo'>gpt-4-turbo</option>"
"<option value='gpt-3.5-turbo'>gpt-3.5-turbo</option>"
"</select></div>"
"<div class='bflds' id='bf1'>"
"<label>WEBHOOK URL</label><input type='text' id='b-nu' placeholder='https://...'>"
"<label>AUTH TOKEN</label><input type='password' id='b-nt' placeholder='opcional'>"
"</div>"
"<div class='bflds' id='bf2'>"
"<label>GATEWAY URL</label><input type='text' id='b-cu' placeholder='https://...'>"
"<label>AUTH TOKEN</label><input type='password' id='b-ct' placeholder='opcional'>"
"</div>"
"<button onclick='saveBrain()'>GUARDAR BRAIN</button>"
"<div class='msg' id='m-brain'></div>"
"</div></div>"
"<div id='p-audio' class='panel'>"
"<div class='card'><h2>AUDIO</h2>"
"<div class='tr'><label>STT (Whisper)</label><input type='checkbox' id='a-w'></div>"
"<div class='tr'><label>TTS (OpenAI)</label><input type='checkbox' id='a-t'></div>"
"<label>API KEY</label><input type='password' id='a-k' placeholder='sk-...'>"
"<label>VOZ TTS</label>"
"<select id='a-v'>"
"<option value='nova'>nova</option><option value='alloy'>alloy</option>"
"<option value='echo'>echo</option><option value='fable'>fable</option>"
"<option value='onyx'>onyx</option><option value='shimmer'>shimmer</option>"
"</select>"
"<label>VELOCIDAD: <span id='sv'>1.0</span>x</label>"
"<input type='range' id='a-s' min='0.25' max='4' step='0.25' value='1' oninput=\"document.getElementById('sv').textContent=this.value\">"
"<label>VOLUMEN: <span id='vv'>80</span>%</label>"
"<input type='range' id='a-vol' min='0' max='100' step='5' value='80' oninput=\"document.getElementById('vv').textContent=this.value\">"
"<button onclick=\"saveAudio()\">GUARDAR AUDIO</button>"
"<div class='msg' id='m-audio'></div>"
"</div></div>"
"<div id='p-pet' class='panel'>"
"<div class='card'><h2>MASCOTA</h2>"
"<div class='pgrid'>"
"<div class='popt s' id='pt0' onclick=\"selP(0,'Kraken')\"><em>(*)</em>Kraken</div>"
"<div class='popt'   id='pt1' onclick=\"selP(1,'Eye')\"><em>(o)</em>Eye</div>"
"<div class='popt'   id='pt2' onclick=\"selP(2,'CRT-Bot')\"><em>[R]</em>CRT-Bot</div>"
"<div class='popt'   id='pt3' onclick=\"selP(3,'Drone')\"><em>^</em>Drone</div>"
"<div class='popt'   id='pt4' onclick=\"selP(4,'Blob')\"><em>~</em>Blob</div>"
"</div>"
"<label>NOMBRE</label><input type='text' id='p-n' placeholder='Kraken' maxlength='31'>"
"<button onclick=\"savePet()\">GUARDAR PET</button>"
"<div class='msg' id='m-pet'></div>"
"</div></div>";

const char WebConfig::PAGE_3[] PROGMEM =
"<script>"
"var curB=0,curP=0;"
"function gi(id){return document.getElementById(id);}"
"function tab(name,el){"
"  document.querySelectorAll('.panel').forEach(function(p){p.classList.remove('a')});"
"  document.querySelectorAll('.tab').forEach(function(t){t.classList.remove('a')});"
"  gi('p-'+name).classList.add('a');"
"  el.classList.add('a');"
"}"
"function selB(n){"
"  curB=n;"
"  for(var i=0;i<3;i++){"
"    gi('bo'+i).classList.toggle('s',i===n);"
"    gi('bf'+i).classList.toggle('a',i===n);"
"  }"
"}"
"function selP(n,name){"
"  curP=n;"
"  for(var i=0;i<5;i++) gi('pt'+i).classList.toggle('s',i===n);"
"  gi('p-n').value=name;"
"}"
"function showMsg(id,txt,ok){"
"  var el=gi('m-'+id);"
"  el.textContent=txt;"
"  el.className='msg '+(ok?'ok':'er');"
"  el.style.display='block';"
"  setTimeout(function(){el.style.display='none';},3000);"
"}"
"function doPost(url,data,cb){"
"  var xhr=new XMLHttpRequest();"
"  xhr.open('POST',url);"
"  xhr.setRequestHeader('Content-Type','application/json');"
"  xhr.onload=function(){cb(JSON.parse(xhr.responseText));};"
"  xhr.send(JSON.stringify(data));"
"}"
"function save(type,data){"
"  doPost('/config/'+type,data,function(r){"
"    var t=!r.ok?'Error':(r.persisted===false?'Guardado en RAM (sin FS)':'Guardado!');"
"    showMsg(type,t,r.ok);"
"  });"
"}"
"function saveBrain(){"
"  doPost('/config/brain',{"
"    provider:curB,"
"    openaiKey:gi('b-ok').value,openaiModel:gi('b-om').value,"
"    n8nWebhookUrl:gi('b-nu').value,n8nAuthToken:gi('b-nt').value,"
"    claudeGatewayUrl:gi('b-cu').value,claudeAuthToken:gi('b-ct').value"
"  },function(r){"
"    var t=!r.ok?'Error':(r.persisted===false?'Brain en RAM (sin FS)':'Brain actualizado!');"
"    showMsg('brain',t,r.ok);"
"  });"
"}"
"function saveAudio(){"
"  doPost('/config/audio',{"
"    whisperEnabled:gi('a-w').checked,ttsEnabled:gi('a-t').checked,"
"    openaiKey:gi('a-k').value,ttsVoice:gi('a-v').value,"
"    ttsSpeed:parseFloat(gi('a-s').value),ttsVolume:parseInt(gi('a-vol').value)"
"  },function(r){"
"    var t=!r.ok?'Error':(r.persisted===false?'Audio en RAM (sin FS)':'Audio guardado!');"
"    showMsg('audio',t,r.ok);"
"  });"
"}"
"function savePet(){"
"  doPost('/config/pet',{type:curP,name:gi('p-n').value||'Kraken'},"
"    function(r){"
"      var t=!r.ok?'Error':(r.persisted===false?'Pet en RAM (sin FS)':'Pet actualizado!');"
"      showMsg('pet',t,r.ok);"
"    });"
"}"
"function loadStatus(){"
"  var xhr=new XMLHttpRequest();"
"  xhr.open('GET','/status');"
"  xhr.onload=function(){"
"    var s=JSON.parse(xhr.responseText);"
"    gi('sw').textContent=s.wifi;"
"    gi('sb').textContent=['OpenAI','N8N','Claude'][s.brain];"
"    gi('sp').textContent=s.petName;"
"    gi('sh').textContent=Math.floor(s.heap/1024)+'KB';"
"  };"
"  xhr.send();"
"}"
"loadStatus();"
"setInterval(loadStatus,5000);"
"</script></body></html>";
