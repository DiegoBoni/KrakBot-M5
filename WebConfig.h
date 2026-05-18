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

    void end() {
        _server.stop();
        Serial.println("[Web] Server stopped");
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

        _server.on("/ping", HTTP_GET, [this]() {
            _server.send(200, "text/plain", "ok");
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
            _cfg->audio.ttsEnabled = doc["ttsEnabled"] | false;
            strlcpy(_cfg->audio.ttsVoice,  doc["ttsVoice"]  | "nova", sizeof(_cfg->audio.ttsVoice));
            _cfg->audio.ttsVolume = doc["ttsVolume"] | 70;
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
        html.reserve(10000);
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
"<meta name='viewport' content='width=device-width,initial-scale=1,maximum-scale=1'>"
"<title>KRAKBOT</title>"
"<style>"
":root{--bg:#060810;--card:#0d0f1a;--border:#1a1d2e;--accent:#00e5cc;--text:#dde4f0;--muted:#4a5068;--ok:#00c97a;--err:#ff3f5f}"
"*{box-sizing:border-box;margin:0;padding:0}"
"html,body{height:100%}"
"body{background:var(--bg);color:var(--text);font-family:'Courier New',monospace;min-height:100vh}"
".topbar{display:flex;align-items:center;justify-content:space-between;padding:14px 20px;border-bottom:1px solid var(--border);position:sticky;top:0;background:var(--bg);z-index:10}"
".logo{color:var(--accent);font-size:1.1rem;letter-spacing:4px;font-weight:bold}"
".ver{color:var(--muted);font-size:.7rem}"
".sbar{display:grid;grid-template-columns:repeat(auto-fit,minmax(100px,1fr));gap:8px;padding:12px 20px;border-bottom:1px solid var(--border)}"
".stat{background:var(--card);border:1px solid var(--border);padding:8px 12px}"
".stat-l{font-size:.6rem;color:var(--muted);letter-spacing:1px;margin-bottom:2px}"
".stat-v{font-size:.82rem;color:var(--accent)}"
".main{display:grid;grid-template-columns:180px 1fr;min-height:calc(100vh - 110px)}"
"@media(max-width:600px){.main{grid-template-columns:1fr;grid-template-rows:auto 1fr}}"
".nav{border-right:1px solid var(--border);padding:16px 0}"
"@media(max-width:600px){.nav{border-right:none;border-bottom:1px solid var(--border);padding:8px;display:flex;gap:4px;overflow-x:auto}}"
".navbtn{display:block;width:100%;text-align:left;padding:10px 20px;background:none;border:none;color:var(--muted);font-family:inherit;font-size:.78rem;letter-spacing:1px;cursor:pointer;border-left:2px solid transparent}"
".navbtn:hover{color:var(--text)}"
".navbtn.a{color:var(--accent);border-left-color:var(--accent);background:rgba(0,229,204,.05)}"
"@media(max-width:600px){.navbtn{border-left:none;border-bottom:2px solid transparent;padding:8px 14px;white-space:nowrap}}"
"@media(max-width:600px){.navbtn.a{border-bottom-color:var(--accent);border-left:none}}"
".content{padding:20px;overflow-y:auto}"
".panel{display:none}.panel.a{display:block}"
".card{background:var(--card);border:1px solid var(--border);padding:18px;margin-bottom:14px}"
".card-title{font-size:.72rem;letter-spacing:2px;color:var(--accent);margin-bottom:16px;padding-bottom:8px;border-bottom:1px solid var(--border)}"
"label{display:block;font-size:.68rem;color:var(--muted);margin-top:12px;margin-bottom:4px;letter-spacing:1px}"
"input[type=text],input[type=password],select{width:100%;background:var(--bg);border:1px solid var(--border);color:var(--text);padding:10px 12px;font-family:inherit;font-size:.85rem;outline:none;border-radius:0}"
"input:focus,select:focus{border-color:var(--accent)}"
"input[type=range]{width:100%;accent-color:var(--accent);margin-top:4px}"
".row{display:flex;justify-content:space-between;align-items:center;margin-top:12px}"
".row label{margin:0}"
"input[type=checkbox]{accent-color:var(--accent);width:18px;height:18px;cursor:pointer}"
"button.save{margin-top:16px;padding:12px;background:transparent;border:1px solid var(--accent);color:var(--accent);font-family:inherit;font-size:.78rem;letter-spacing:2px;cursor:pointer;width:100%}"
"button.save:hover{background:var(--accent);color:var(--bg)}"
".pgrid{display:grid;grid-template-columns:repeat(5,1fr);gap:6px;margin-top:10px}"
"@media(max-width:400px){.pgrid{grid-template-columns:repeat(3,1fr)}}"
".popt{background:var(--bg);border:1px solid var(--border);padding:12px 4px;text-align:center;cursor:pointer;font-size:.65rem;color:var(--muted)}"
".popt:hover,.popt.s{border-color:var(--accent);color:var(--accent)}"
".popt em{font-size:1.5rem;display:block;margin-bottom:4px;font-style:normal}"
".bopts{display:flex;flex-direction:column;gap:6px;margin-top:10px}"
".bopt{display:flex;align-items:center;gap:12px;padding:12px;background:var(--bg);border:1px solid var(--border);cursor:pointer}"
".bopt:hover,.bopt.s{border-color:var(--accent)}"
".bopt-info strong{display:block;font-size:.82rem;margin-bottom:2px}"
".bopt-info small{color:var(--muted);font-size:.68rem}"
".bflds{display:none;margin-top:14px}.bflds.a{display:block}"
".msg{padding:10px;font-size:.75rem;margin-top:8px;display:none;letter-spacing:.5px}"
".msg.ok{color:var(--ok);border:1px solid var(--ok)}"
".msg.er{color:var(--err);border:1px solid var(--err)}"
".chat-box{display:flex;gap:8px;margin-bottom:14px}"
".chat-box input{flex:1;background:var(--bg);border:1px solid var(--border);color:var(--text);padding:10px 12px;font-family:inherit;font-size:.85rem;outline:none}"
".chat-box input:focus{border-color:var(--accent)}"
".chat-box button{padding:10px 16px;background:transparent;border:1px solid var(--accent);color:var(--accent);font-family:inherit;cursor:pointer;font-size:.8rem}"
".chat-box button:hover{background:var(--accent);color:var(--bg)}"
"</style></head><body>"
"<div class='topbar'><span class='logo'>KRAKBOT</span><span class='ver'>v";

const char WebConfig::PAGE_2[] PROGMEM =
"</span></div>"
"<div class='sbar'>"
"<div class='stat'><div class='stat-l'>WIFI</div><div class='stat-v' id='sw'>...</div></div>"
"<div class='stat'><div class='stat-l'>BRAIN</div><div class='stat-v' id='sb'>-</div></div>"
"<div class='stat'><div class='stat-l'>PET</div><div class='stat-v' id='sp'>-</div></div>"
"<div class='stat'><div class='stat-l'>HEAP</div><div class='stat-v' id='sh'>-</div></div>"
"</div>"
"<div class='main'>"
"<nav class='nav'>"
"<button class='navbtn a' onclick=\"tab('wifi',this)\">WiFi</button>"
"<button class='navbtn' onclick=\"tab('brain',this)\">Brain</button>"
"<button class='navbtn' onclick=\"tab('audio',this)\">Audio</button>"
"<button class='navbtn' onclick=\"tab('pet',this)\">Pet</button>"
"<button class='navbtn' onclick=\"tab('chat',this)\">Chat</button>"
"</nav>"
"<div class='content'>"
"<div id='p-wifi' class='panel a'>"
"<div class='card'><div class='card-title'>WI-FI</div>"
"<label>SSID</label><input type='text' id='w-s' placeholder='MiRed'>"
"<label>PASSWORD</label><input type='password' id='w-p' placeholder='...'>"
"<button class='save' onclick=\"save('wifi',{ssid:gi('w-s').value,password:gi('w-p').value})\">GUARDAR</button>"
"<div class='msg' id='m-wifi'></div>"
"</div></div>"
"<div id='p-brain' class='panel'>"
"<div class='card'><div class='card-title'>BRAIN PROVIDER</div>"
"<div class='bopts'>"
"<label class='bopt s' id='bo0'><input type='radio' name='br' checked onchange='selB(0)'><div class='bopt-info'><strong>OpenAI Direct</strong><small>API key standalone</small></div></label>"
"<label class='bopt' id='bo1'><input type='radio' name='br' onchange='selB(1)'><div class='bopt-info'><strong>N8N Webhook</strong><small>Agentes / workflows</small></div></label>"
"<label class='bopt' id='bo2'><input type='radio' name='br' onchange='selB(2)'><div class='bopt-info'><strong>Claude Gateway</strong><small>Dev companion</small></div></label>"
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
"<button class='save' onclick='saveBrain()'>GUARDAR BRAIN</button>"
"<div class='msg' id='m-brain'></div>"
"</div></div>"
"<div id='p-audio' class='panel'>"
"<div class='card'><div class='card-title'>AUDIO</div>"
"<div class='row'><label>TTS (OpenAI)</label><input type='checkbox' id='a-t'></div>"
"<label>API KEY</label><input type='password' id='a-k' placeholder='sk-...'>"
"<label>VOZ TTS</label>"
"<select id='a-v'>"
"<option value='nova'>nova</option><option value='alloy'>alloy</option>"
"<option value='echo'>echo</option><option value='fable'>fable</option>"
"<option value='onyx'>onyx</option><option value='shimmer'>shimmer</option>"
"</select>"
"<label>VOLUMEN: <span id='vv'>70</span>%</label>"
"<input type='range' id='a-vol' min='0' max='100' step='5' value='70' oninput=\"gi('vv').textContent=this.value\">"
"<button class='save' onclick='saveAudio()'>GUARDAR AUDIO</button>"
"<div class='msg' id='m-audio'></div>"
"</div></div>"
"<div id='p-pet' class='panel'>"
"<div class='card'><div class='card-title'>MASCOTA</div>"
"<div class='pgrid'>"
"<div class='popt s' id='pt0' onclick=\"selP(0,'Kraken')\"><em>(*)</em>Kraken</div>"
"<div class='popt' id='pt1' onclick=\"selP(1,'Eye')\"><em>(o)</em>Eye</div>"
"<div class='popt' id='pt2' onclick=\"selP(2,'CRTBot')\"><em>[R]</em>CRTBot</div>"
"<div class='popt' id='pt3' onclick=\"selP(3,'Drone')\"><em>^</em>Drone</div>"
"<div class='popt' id='pt4' onclick=\"selP(4,'Blob')\"><em>~</em>Blob</div>"
"</div>"
"<label>NOMBRE</label><input type='text' id='p-n' placeholder='Kraken' maxlength='31'>"
"<button class='save' onclick='savePet()'>GUARDAR PET</button>"
"<div class='msg' id='m-pet'></div>"
"</div></div>"
"<div id='p-chat' class='panel'>"
"<div class='card'><div class='card-title'>ENVIAR MENSAJE</div>"
"<div class='chat-box'>"
"<input type='text' id='c-m' placeholder='Escribi algo...' onkeydown=\"if(event.key==='Enter')sendChat()\">"
"<button onclick='sendChat()'>SEND</button>"
"</div>"
"<div class='msg' id='m-chat'></div>"
"</div></div>"
"</div></div>";

const char WebConfig::PAGE_3[] PROGMEM =
"<script>"
"var curB=0,curP=0;"
"function gi(id){return document.getElementById(id);}"
"function tab(name,el){"
"  document.querySelectorAll('.panel').forEach(function(p){p.classList.remove('a')});"
"  document.querySelectorAll('.navbtn').forEach(function(b){b.classList.remove('a')});"
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
"  xhr.onload=function(){try{cb(JSON.parse(xhr.responseText));}catch(e){cb({ok:false});}};"
"  xhr.onerror=function(){cb({ok:false});};"
"  xhr.send(JSON.stringify(data));"
"}"
"function save(type,data){"
"  doPost('/config/'+type,data,function(r){"
"    showMsg(type,r.ok?(r.persisted===false?'RAM (sin FS)':'Guardado!'):'Error',r.ok);"
"  });"
"}"
"function saveBrain(){"
"  doPost('/config/brain',{"
"    provider:curB,"
"    openaiKey:gi('b-ok').value,openaiModel:gi('b-om').value,"
"    n8nWebhookUrl:gi('b-nu').value,n8nAuthToken:gi('b-nt').value,"
"    claudeGatewayUrl:gi('b-cu').value,claudeAuthToken:gi('b-ct').value"
"  },function(r){"
"    showMsg('brain',r.ok?(r.persisted===false?'Brain en RAM':'Brain actualizado!'):'Error',r.ok);"
"  });"
"}"
"function saveAudio(){"
"  doPost('/config/audio',{"
"    ttsEnabled:gi('a-t').checked,"
"    openaiKey:gi('a-k').value,ttsVoice:gi('a-v').value,"
"    ttsVolume:parseInt(gi('a-vol').value)"
"  },function(r){"
"    showMsg('audio',r.ok?(r.persisted===false?'Audio en RAM':'Audio guardado!'):'Error',r.ok);"
"  });"
"}"
"function savePet(){"
"  doPost('/config/pet',{type:curP,name:gi('p-n').value||'Kraken'},function(r){"
"    showMsg('pet',r.ok?(r.persisted===false?'Pet en RAM':'Pet actualizado!'):'Error',r.ok);"
"  });"
"}"
"function sendChat(){"
"  var txt=gi('c-m').value.trim();"
"  if(!txt)return;"
"  doPost('/message',{text:txt},function(r){"
"    showMsg('chat',r.ok?'Enviado!':'Error al enviar',r.ok);"
"    if(r.ok)gi('c-m').value='';"
"  });"
"}"
"function loadStatus(){"
"  var xhr=new XMLHttpRequest();"
"  xhr.open('GET','/status');"
"  xhr.onload=function(){"
"    try{"
"      var s=JSON.parse(xhr.responseText);"
"      gi('sw').textContent=s.wifi;"
"      gi('sb').textContent=['OpenAI','N8N','Claude'][s.brain]||'-';"
"      gi('sp').textContent=s.petName;"
"      gi('sh').textContent=Math.floor(s.heap/1024)+'KB';"
"    }catch(e){}"
"  };"
"  xhr.send();"
"}"
"loadStatus();"
"setInterval(loadStatus,5000);"
"</script></body></html>";
