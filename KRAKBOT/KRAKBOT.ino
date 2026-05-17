// ═══════════════════════════════════════════════════════════
//  KRAKBOT — Companion OS for M5Stack Cardputer
//  Estética: krakbot.app | verde turquesa | terminal moderna
// ═══════════════════════════════════════════════════════════

#include <M5Cardputer.h>
#include <LittleFS.h>
#include <math.h>

#include "config.h"
#include "Storage.h"
#include "WifiManager.h"
#include "Pet.h"
#include "BrainManager.h"
#include "WebConfig.h"

// Paleta krakbot.app
#define KRAKEN_RED   0x07F9   // verde turquesa — acento principal UI
#define KRAKEN_DIM   0x0454   // verde oscuro — IP / secundarios
#define KRAKEN_GLOW  0x07FF   // cyan — pensando / énfasis

// ── Globals ──────────────────────────────────────────────────────────────────
AppConfig g_cfg;
String    g_pendingMessage = "";
bool      g_waitingReply   = false;

M5Canvas canvas(&M5Cardputer.Display);
bool g_canvasReady = false;

Pet          g_pet;
BrainManager g_brain;
WebConfig    g_web;

// ── Chat history ─────────────────────────────────────────────────────────────
struct ChatEntry { String role; String text; };
static const int MAX_HISTORY = 20;
static ChatEntry g_history[MAX_HISTORY];
static int       g_historyCount  = 0;
static int       g_scrollOffset  = 0;

void addHistory(const String& role, const String& text) {
    if (g_historyCount < MAX_HISTORY) {
        g_history[g_historyCount++] = {role, text};
    } else {
        for (int i = 0; i < MAX_HISTORY - 1; i++) g_history[i] = g_history[i + 1];
        g_history[MAX_HISTORY - 1] = {role, text};
    }
    g_scrollOffset = 0;
}

// ── UI state ─────────────────────────────────────────────────────────────────
String   g_inputBuffer = "";
String   g_statusLine  = "iniciando...";
uint32_t g_idleAt      = 0;
bool     g_webReady    = false;

// ── FreeRTOS dual-core ────────────────────────────────────────────────────────
// chat HTTP en Core 0, UI/WebServer en Core 1
static QueueHandle_t chatQueue;
static QueueHandle_t replyQueue;

void chatTask(void*) {
    String* msgPtr;
    while (true) {
        if (xQueueReceive(chatQueue, &msgPtr, portMAX_DELAY) == pdTRUE) {
            String resp = g_brain.chat(*msgPtr);
            delete msgPtr;
            String* replyPtr = new String(resp);
            xQueueSend(replyQueue, &replyPtr, portMAX_DELAY);
        }
    }
}

// ── Boot screen animado ───────────────────────────────────────────────────────
static void drawBootScreen(const String& msg, int frame = 0) {
    if (!g_canvasReady) return;
    const int W = M5Cardputer.Display.width();
    const int H = M5Cardputer.Display.height();

    canvas.fillScreen(0x0841);
    canvas.setFont(&fonts::Font0);

    // "KRAKBOT" size 3 → 126px ancho
    canvas.setTextSize(3);
    canvas.setTextColor(KRAKEN_RED);
    canvas.setCursor((W - 126) / 2, H / 2 - 32);
    canvas.print("KRAKBOT");

    // "companion OS"
    canvas.setTextSize(1);
    canvas.setTextColor(KRAKEN_DIM);
    canvas.setCursor((W - 72) / 2, H / 2 - 6);
    canvas.print("companion OS");

    // Barra de progreso animada
    int barW = W - 48;
    int barX = 24;
    int barY = H / 2 + 10;
    canvas.drawRect(barX, barY, barW, 4, 0x1082);
    float t = (sinf(frame * 0.22f) + 1.0f) * 0.5f;
    canvas.fillRect(barX, barY, (int)(t * barW), 4, KRAKEN_RED);

    // Mensaje de estado
    canvas.setTextSize(1);
    canvas.setTextColor(0x8410);
    int mw = msg.length() * 6;
    canvas.setCursor((W - mw) / 2, H / 2 + 22);
    canvas.print(msg);

    canvas.pushSprite(0, 0);
}

// ── ASCII sanitizer para FreeMono (no soporta UTF-8) ─────────────────────────
// Convierte acentos y caracteres latinos a ASCII. Solo para display.
static String toASCII(const String& s) {
    String out;
    out.reserve(s.length());
    const uint8_t* p = (const uint8_t*)s.c_str();
    while (*p) {
        if (*p < 0x80) {
            out += (char)*p++;
        } else if (*p == 0xC3) {
            p++;
            switch (*p) {
                case 0xA0: case 0xA1: case 0xA2: case 0xA3: case 0xA4: case 0xA5: out += 'a'; break;
                case 0x80: case 0x81: case 0x82: case 0x83: case 0x84: case 0x85: out += 'A'; break;
                case 0xA8: case 0xA9: case 0xAA: case 0xAB: out += 'e'; break;
                case 0x88: case 0x89: case 0x8A: case 0x8B: out += 'E'; break;
                case 0xAC: case 0xAD: case 0xAE: case 0xAF: out += 'i'; break;
                case 0x8C: case 0x8D: case 0x8E: case 0x8F: out += 'I'; break;
                case 0xB2: case 0xB3: case 0xB4: case 0xB5: case 0xB6: out += 'o'; break;
                case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: out += 'O'; break;
                case 0xB9: case 0xBA: case 0xBB: case 0xBC: out += 'u'; break;
                case 0x99: case 0x9A: case 0x9B: case 0x9C: out += 'U'; break;
                case 0xB1: out += 'n'; break;  // ñ
                case 0x91: out += 'N'; break;  // Ñ
                default:   out += '?'; break;
            }
            p++;
        } else {
            if      ((*p & 0xE0) == 0xC0) p += 2;
            else if ((*p & 0xF0) == 0xE0) p += 3;
            else if ((*p & 0xF8) == 0xF0) p += 4;
            else p++;
        }
    }
    return out;
}

// ── Word-wrap helper ──────────────────────────────────────────────────────────
static int wrapText(const String& text, int charW, int maxW,
                    String* lines, int maxLines) {
    int charsPerLine = maxW / charW;
    if (charsPerLine < 1) charsPerLine = 1;
    int count = 0;
    String rem = text;
    while (rem.length() > 0 && count < maxLines) {
        String line = rem.substring(0, charsPerLine);
        if ((int)rem.length() > charsPerLine) {
            int sp = line.lastIndexOf(' ');
            if (sp > 0) line = rem.substring(0, sp);
        }
        lines[count++] = line;
        rem = rem.substring(line.length());
        if (rem.startsWith(" ")) rem = rem.substring(1);
    }
    return count;
}

// ── Draw UI ───────────────────────────────────────────────────────────────────
void drawUI() {
    if (!g_canvasReady) return;

    const int W = M5Cardputer.Display.width();   // 240
    const int H = M5Cardputer.Display.height();  // 135
    const uint16_t accent = KRAKEN_RED;

    canvas.fillScreen(0x0841);

    // ── HEADER: Font0 (8bit pixel) ───────────────────────────────────────────
    canvas.setFont(&fonts::Font0);

    // Mascota izquierda
    g_pet.draw(2, 2);

    // Nombre alineado a la derecha — Font0 size 2 → 12px/char
    String petName = String(g_cfg.pet.name);
    int nameW = petName.length() * 12;
    canvas.setTextSize(2);
    canvas.setTextColor(accent);
    canvas.setCursor(W - nameW - 4, 4);
    canvas.print(petName);

    // IP alineada a la derecha — Font0 size 1 → 6px/char
    String ipStr = WifiManager::isAPMode() ? "192.168.4.1" : WifiManager::localIP();
    canvas.setTextSize(1);
    canvas.setTextColor(KRAKEN_DIM);
    canvas.setCursor(W - (int)ipStr.length() * 6 - 4, 24);
    canvas.print(ipStr);

    // Status dinámico alineado a la derecha
    String status;
    if (g_waitingReply) {
        uint8_t d = (millis() / 350) % 4;
        status = "pensando";
        for (uint8_t i = 0; i < d; i++) status += ".";
    } else {
        status = g_statusLine;
    }
    canvas.setTextSize(1);
    canvas.setTextColor(g_waitingReply ? KRAKEN_GLOW : 0x4A69);
    canvas.setCursor(W - (int)status.length() * 6 - 4, 34);
    canvas.print(status);

    // Separador
    canvas.drawLine(0, 48, W, 48, 0x1082);

    // ── CHAT: FreeMono9pt7b ───────────────────────────────────────────────────
    canvas.setFont(&fonts::FreeMono9pt7b);
    const int chatTop  = 52;
    const int chatBot  = H - 22;
    const int lineH    = 18;
    const int charW    = 11;
    const int maxLines = (chatBot - chatTop) / lineH;
    const int chatW    = W - 6;

    static String    allLines[80];
    static uint16_t  allColors[80];
    int totalLines = 0;

    for (int e = 0; e < g_historyCount && totalLines < 76; e++) {
        bool isUser   = (g_history[e].role == "user");
        uint16_t col  = isUser ? 0xA534 : TFT_WHITE;
        String prefix = isUser ? "> " : "  ";
        String full   = prefix + toASCII(g_history[e].text);
        String wrapped[20];
        int n = wrapText(full, charW, chatW, wrapped, 20);
        for (int i = 0; i < n && totalLines < 76; i++) {
            allLines[totalLines]  = wrapped[i];
            allColors[totalLines] = col;
            totalLines++;
        }
    }

    // Puntos de "pensando" al final
    if (g_waitingReply && totalLines < 78) {
        uint8_t d = (millis() / 350) % 4;
        String dots = "  ";
        for (uint8_t i = 0; i <= d; i++) dots += ".";
        allLines[totalLines]  = dots;
        allColors[totalLines] = KRAKEN_GLOW;
        totalLines++;
    }

    int endLine   = totalLines - g_scrollOffset;
    int startLine = endLine - maxLines;
    if (startLine < 0) startLine = 0;
    if (endLine > totalLines) endLine = totalLines;

    canvas.setTextSize(1);
    int cy = chatTop + 13;  // baseline FreeMono ~13px desde el top
    for (int i = startLine; i < endLine; i++) {
        canvas.setTextColor(allColors[i]);
        canvas.setCursor(3, cy);
        canvas.print(allLines[i]);
        cy += lineH;
    }

    // Flecha scroll
    if (g_scrollOffset > 0) {
        canvas.setFont(&fonts::Font0);
        canvas.setTextSize(1);
        canvas.setTextColor(accent);
        canvas.setCursor(W - 8, chatTop);
        canvas.print("^");
    }

    // ── INPUT: Font0 size 2 ──────────────────────────────────────────────────
    canvas.setFont(&fonts::Font0);
    canvas.drawLine(0, H - 20, W, H - 20, 0x1082);
    canvas.setTextSize(2);
    canvas.setTextColor(accent);
    canvas.setCursor(2, H - 17);
    canvas.print(">");

    // Input dinámico: últimos chars que entran — Font0 size 2 → 12px/char
    const int inputCharW  = 12;
    int maxInputChars     = (W - 18) / inputCharW;
    String visible = g_inputBuffer;
    if ((int)visible.length() > maxInputChars)
        visible = visible.substring(visible.length() - maxInputChars);
    canvas.setTextColor(TFT_WHITE);
    canvas.setCursor(16, H - 17);
    canvas.print(visible);

    // Cursor parpadeante
    if ((millis() / 500) % 2 == 0) {
        int cx = 16 + (int)visible.length() * inputCharW;
        canvas.fillRect(cx, H - 17, 10, 14, accent);
    }

    canvas.pushSprite(0, 0);
}

// ── Send message ──────────────────────────────────────────────────────────────
void sendMessage(const String& text) {
    if (text.isEmpty() || g_waitingReply) return;
    addHistory("user", text);
    g_waitingReply = true;
    g_statusLine   = "pensando...";
    g_pet.setState(PET_THINKING);
    String* msgPtr = new String(text);
    xQueueSend(chatQueue, &msgPtr, 0);
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
    auto cfg = M5.config();
    cfg.fallback_board  = m5::board_t::board_M5CardputerADV;
    cfg.internal_imu    = false;
    cfg.internal_mic    = false;
    cfg.internal_spk    = false;
    cfg.internal_rtc    = false;
    cfg.output_power    = false;
    cfg.clear_display   = true;
    cfg.serial_baudrate = 115200;

    M5Cardputer.begin(cfg, true);
    M5Cardputer.Display.setRotation(1);
    delay(200);

    g_canvasReady = canvas.createSprite(
        M5Cardputer.Display.width(),
        M5Cardputer.Display.height()
    ) != nullptr;
    canvas.setFont(&fonts::Font0);

    g_pet.setCanvas(&canvas);
    g_pet.setType(PET_KRAKEN);
    g_pet.setState(PET_IDLE);

    Storage::begin();
    Storage::loadAll(g_cfg);
    g_pet.setType(g_cfg.pet.type);
    g_brain.setConfig(g_cfg.brain);

    chatQueue  = xQueueCreate(1, sizeof(String*));
    replyQueue = xQueueCreate(1, sizeof(String*));
    xTaskCreatePinnedToCore(chatTask, "chatTask", 8192, nullptr, 1, nullptr, 0);

    // Boot animado mientras conecta WiFi
    for (int f = 0; f < 40; f++) {
        drawBootScreen("Conectando WiFi...", f);
        delay(50);
    }

    bool wifiOk = WifiManager::connectToSaved(g_cfg.wifi, 8000);
    if (!wifiOk) {
        WifiManager::startAP();
        g_statusLine = "AP: 192.168.4.1";
        addHistory("bot", "Conectate a KRAKBOT-SETUP y entra a 192.168.4.1 para configurar.");
    } else {
        if (g_brain.hasCredentials()) {
            g_statusLine = "online";
            addHistory("bot", String("Hola! Soy ") + g_cfg.pet.name + ". En que te ayudo?");
        } else {
            g_statusLine = "sin brain";
            addHistory("bot", "Conectado! Configura el Brain en " + WifiManager::localIP());
        }
    }

    g_web.onSave([&]() {
        g_brain.setConfig(g_cfg.brain);
        g_pet.setType(g_cfg.pet.type);
        g_statusLine = g_brain.hasCredentials() ? "online" : "sin brain";
    });
    g_web.begin(g_cfg);
    g_webReady = true;

    Serial.printf("[KRAKBOT] Ready! IP: %s\n", WifiManager::localIP().c_str());
    g_pet.setState(PET_IDLE);
    drawUI();
}

// ── Loop ──────────────────────────────────────────────────────────────────────
void loop() {
    M5Cardputer.update();
    g_web.handle();

    // Respuesta del chatTask (Core 0)
    String* replyPtr = nullptr;
    if (xQueueReceive(replyQueue, &replyPtr, 0) == pdTRUE && replyPtr) {
        String resp = *replyPtr;
        delete replyPtr;
        g_waitingReply = false;
        if (resp.isEmpty()) {
            String err = g_brain.lastError().isEmpty() ? "Sin respuesta." : g_brain.lastError();
            addHistory("bot", err);
            g_statusLine = "error";
            g_pet.setState(PET_ERROR);
        } else {
            addHistory("bot", resp);
            g_statusLine = "online";
            g_pet.setState(PET_TALKING);
            g_idleAt = millis() + 3500;
        }
    }

    // Teclado
    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
        Keyboard_Class::KeysState ks = M5Cardputer.Keyboard.keysState();
        bool typed = false;

        if (ks.enter) {
            String msg = g_inputBuffer;
            g_inputBuffer = "";
            sendMessage(msg);
        }
        if (ks.del && g_inputBuffer.length() > 0) {
            g_inputBuffer.remove(g_inputBuffer.length() - 1);
            typed = true;
        }
        for (char c : ks.word) {
            if (c >= 32 && g_inputBuffer.length() < 200) {
                g_inputBuffer += c;
                typed = true;
            }
        }

        // Fn+; scroll arriba | Fn+. scroll abajo
        if (ks.fn) {
            for (char c : ks.word) {
                if (c == ';') {
                    g_scrollOffset++;
                    if (g_scrollOffset > g_historyCount * 5) g_scrollOffset = g_historyCount * 5;
                }
                if (c == '.') {
                    if (g_scrollOffset > 0) g_scrollOffset--;
                }
            }
        }

        if (typed) {
            g_statusLine = "escribiendo...";
            g_pet.setState(PET_LISTENING);
        }
    }

    // Mensaje desde web panel
    if (!g_pendingMessage.isEmpty() && !g_waitingReply) {
        String msg = g_pendingMessage;
        g_pendingMessage = "";
        sendMessage(msg);
    }

    // Volver a idle
    if (g_idleAt > 0 && millis() > g_idleAt) {
        g_idleAt = 0;
        g_statusLine = WifiManager::isAPMode() ? "AP: 192.168.4.1"
                     : (g_brain.hasCredentials() ? "online" : "sin brain");
        g_pet.setState(PET_IDLE);
    }

    g_pet.update();
    drawUI();
    delay(33);
}
