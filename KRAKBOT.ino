// ═══════════════════════════════════════════════
//  KRAKBOT — Companion OS for M5Stack Cardputer
//  v0.1.0
//
//  Librerías requeridas (instalar desde Library Manager):
//    - M5Cardputer  (by M5Stack)
//    - ArduinoJson  (by Benoit Blanchon)
//    - LovyanGFX    (by lovyan03)
//
//  Board: "M5Stack-Stamps3" con:
//    - Flash: 8MB
//    - PSRAM: OPI PSRAM
//    - Partition: Default 8MB with spiffs
//    - Upload speed: 1500000
// ═══════════════════════════════════════════════

#include <M5Cardputer.h>
#include <LittleFS.h>

#include "config.h"
#include "Storage.h"
#include "WifiManager.h"
#include "Pet.h"
#include "BrainManager.h"
#include "WebConfig.h"

// ─── Globals (declarados en config.h como extern) ─
AppConfig g_cfg;
String    g_pendingMessage = "";
bool      g_waitingReply   = false;

// ─── Display / Sprite ─────────────────────────────
M5Canvas canvas(&M5Cardputer.Display);
bool g_canvasReady = false;

// ─── Módulos ──────────────────────────────────────
Pet         g_pet;
BrainManager g_brain;
WebConfig   g_web;

// ─── Estado UI ────────────────────────────────────
String   g_inputBuffer  = "";
String   g_lastResponse = "Hola! Configurame desde el navegador.";
uint32_t g_idleAt       = 0;   // millis() en que volvemos a idle (0 = ya)

// ─────────────────────────────────────────────────
// UI
// ─────────────────────────────────────────────────
void drawUI() {
    if (!g_canvasReady) return;
    int W = M5Cardputer.Display.width();
    int H = M5Cardputer.Display.height();
    uint16_t accent = g_pet.getAccentColor();

    canvas.fillScreen(TFT_BLACK);

    // ── Mascota ───────────────────────────────────
    g_pet.draw(4, 6);

    // ── Nombre + IP ───────────────────────────────
    canvas.setTextColor(accent);
    canvas.setTextSize(2);
    canvas.setCursor(64, 6);
    canvas.print(g_cfg.pet.name);

    canvas.setTextColor(0x4444);
    canvas.setTextSize(1);
    canvas.setCursor(64, 26);
    String ip = WifiManager::isAPMode()
        ? String("AP: ") + KRAKBOT_AP_SSID
        : WifiManager::localIP();
    canvas.print(ip);

    // ── Separador ─────────────────────────────────
    canvas.drawLine(0, 52, W, 52, 0x1111);

    // ── Respuesta (wrap simple 16 chars) ──────────
    canvas.setTextColor(TFT_WHITE);
    canvas.setTextSize(2);
    String resp = g_lastResponse;
    int ty = 58;
    const int maxCh = 16;
    while (resp.length() > 0 && ty < H - 26) {
        String line = resp.substring(0, maxCh);
        if ((int)resp.length() > maxCh) {
            int sp = line.lastIndexOf(' ');
            if (sp > 0) line = resp.substring(0, sp);
        }
        canvas.setCursor(4, ty);
        canvas.print(line);
        resp = resp.substring(line.length());
        if (resp.startsWith(" ")) resp = resp.substring(1);
        ty += 16;
    }

    // ── Thinking indicator ────────────────────────
    if (g_waitingReply) {
        canvas.setTextColor(accent);
        uint8_t dots = (millis() / 400) % 4;
        canvas.setCursor(4, ty);
        for (uint8_t i = 0; i <= dots; i++) canvas.print(".");
    }

    // ── Input bar ─────────────────────────────────
    canvas.drawLine(0, H - 22, W, H - 22, 0x1111);
    canvas.setTextColor(accent);
    canvas.setTextSize(2);
    canvas.setCursor(4, H - 16);
    canvas.print("> ");
    canvas.setTextColor(TFT_WHITE);
    canvas.print(g_inputBuffer);
    if ((millis() / 500) % 2 == 0) canvas.print("_");

    canvas.pushSprite(0, 0);
}

void drawBootScreen(const char* title, const char* detail = nullptr, uint16_t color = TFT_CYAN) {
    M5Cardputer.Display.fillScreen(TFT_BLACK);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(color, TFT_BLACK);
    M5Cardputer.Display.setCursor(6, 10);
    M5Cardputer.Display.println(title);
    if (detail) {
        M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
        M5Cardputer.Display.setCursor(6, 28);
        M5Cardputer.Display.println(detail);
    }
}

// ─────────────────────────────────────────────────
// Envío de mensaje al brain
// ─────────────────────────────────────────────────
void sendMessage(const String& text) {
    if (text.isEmpty()) return;
    g_waitingReply = true;
    g_lastResponse = "";
    g_pet.setState(PET_THINKING);
    drawUI();   // mostrar thinking inmediatamente

    String resp = g_brain.chat(text);

    if (resp.isEmpty()) {
        g_lastResponse = g_brain.lastError().isEmpty()
            ? "Sin respuesta." : g_brain.lastError();
        g_pet.setState(PET_ERROR);
    } else {
        g_lastResponse = resp;
        g_pet.setState(PET_TALKING);
        g_idleAt = millis() + 4000;
    }
    g_waitingReply = false;
}

// ─────────────────────────────────────────────────
// SETUP
// ─────────────────────────────────────────────────
void setup() {
    auto cfg = M5.config();
    cfg.fallback_board = m5::board_t::board_M5CardputerADV;
    cfg.internal_imu = false;
    cfg.internal_mic = false;
    cfg.internal_spk = false;
    cfg.internal_rtc = false;
    cfg.output_power = false;
    cfg.clear_display = true;
    cfg.serial_baudrate = 115200;
    M5Cardputer.begin(cfg, true);
    M5Cardputer.Display.setRotation(1);
    delay(200);
    Serial.println("\n[KRAKBOT] Booting v" KRAKBOT_VERSION);
    drawBootScreen("KRAKBOT", "Init display");

    // Sprite doble buffer
    g_canvasReady = canvas.createSprite(
        M5Cardputer.Display.width(),
        M5Cardputer.Display.height()
    ) != nullptr;
    canvas.setFont(&fonts::Font0);
    if (!g_canvasReady) {
        drawBootScreen("KRAKBOT", "Sprite alloc failed", TFT_RED);
        Serial.println("[KRAKBOT] Sprite allocation failed");
    }

    // Módulos
    g_pet.setCanvas(&canvas);
    bool storageOk = Storage::begin();
    Storage::loadAll(g_cfg);
    if (!storageOk) {
        drawBootScreen("KRAKBOT", "LittleFS unavailable");
        delay(500);
    }

    g_pet.setType(g_cfg.pet.type);
    g_brain.setConfig(g_cfg.brain);

    // Wi-Fi
    drawBootScreen("KRAKBOT", "Connecting WiFi");
    bool wifiOk = WifiManager::connectToSaved(g_cfg.wifi, 8000);
    if (!wifiOk) {
        WifiManager::startAP();
        g_lastResponse = "Conectate a KRAKBOT-SETUP / pass: krakbot42 / luego entra a 192.168.4.1";
        g_pet.setState(PET_OFFLINE);
    } else {
        g_lastResponse = String("Listo! Soy ") + g_cfg.pet.name + ". En que te ayudo?";
        g_pet.setState(PET_IDLE);
    }

    // Web config
    g_web.onSave([&]() {
        g_brain.setConfig(g_cfg.brain);
        g_pet.setType(g_cfg.pet.type);
        strlcpy(g_cfg.pet.name, g_cfg.pet.name, sizeof(g_cfg.pet.name));
    });
    g_web.begin(g_cfg);

    Serial.println("[KRAKBOT] Ready!");
}

// ─────────────────────────────────────────────────
// LOOP
// ─────────────────────────────────────────────────
void loop() {
    M5Cardputer.update();

    // ── Web server ────────────────────────────────
    g_web.handle();

    // ── Teclado ───────────────────────────────────
    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
        Keyboard_Class::KeysState ks = M5Cardputer.Keyboard.keysState();
        bool typed = false;

        if (ks.enter) {
            String msg = g_inputBuffer;
            g_inputBuffer = "";
            sendMessage(msg);
        }
        if (ks.del) {
            if (g_inputBuffer.length() > 0) {
                g_inputBuffer.remove(g_inputBuffer.length() - 1);
                typed = true;
            }
        }

        for (char c : ks.word) {
            if (c >= 32 && g_inputBuffer.length() < 100) {
                g_inputBuffer += c;
                typed = true;
            }
        }
        if (typed) g_pet.setState(PET_LISTENING);
    }

    // ── Mensaje desde web ─────────────────────────
    if (!g_pendingMessage.isEmpty() && !g_waitingReply) {
        String msg = g_pendingMessage;
        g_pendingMessage = "";
        sendMessage(msg);
    }

    // ── Volver a idle ─────────────────────────────
    if (g_idleAt > 0 && millis() > g_idleAt) {
        g_idleAt = 0;
        g_pet.setState(PET_IDLE);
    }

    // ── Animación mascota ─────────────────────────
    g_pet.update();

    // ── Render ────────────────────────────────────
    drawUI();

    delay(33);  // ~30fps
}
