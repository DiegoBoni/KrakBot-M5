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
// #include "AudioManager.h"  // pendiente — conflicto I2S con M5
struct AudioManager {
    bool begin(int v=70)     { return true; }
    void setTTS(bool e)      {}
    bool getTTS() const      { return false; }
    void setVolume(int v)    {}
    int  getVolume() const   { return 70; }
    bool startRecording()    { return false; }
    void updateRecording()   {}
    void stopRecording()     {}
    bool isRecording() const { return false; }
    bool speak(const String&, const char*, const char* v="nova") { return false; }
    String transcribe(const char*) { return ""; }
    String lastError() const { return ""; }
    static constexpr const char* VOICES[] = {"nova","alloy"};
    static constexpr int VOICE_COUNT = 2;
};

// ── Sound toggle (declarado antes del namespace Sound) ────────────────────────
static bool g_soundEnabled = true;

// ── Sonidos via M5Speaker API ─────────────────────────────────────────────────
// Cada mascota tiene su firma sonora: frecuencias y duraciones distintas
namespace Sound {
    // Tono simple helper
    static void tone(uint32_t freq, uint32_t ms) {
        if (!g_soundEnabled) return;
        M5Cardputer.Speaker.tone(freq, ms);
        delay(ms + 10);
        M5Cardputer.Speaker.stop();
    }

    // Beep al enviar mensaje — corto y seco
    static void onSend(PetType pet) {
        switch (pet) {
            case PET_KRAKEN:  // bloop descendente
                tone(880, 60); tone(440, 60);
                break;
            case PET_EYE:     // bip agudo único
                tone(1200, 80);
                break;
            case PET_CRTBOT:  // dos pulsos cuadrados retro
                tone(600, 40); delay(20); tone(600, 40);
                break;
            case PET_DRONE:   // zumbido corto
                tone(200, 100);
                break;
            case PET_BLOB:    // burbuja ascendente
                tone(300, 50); tone(500, 50); tone(700, 50);
                break;
        }
    }

    // Vocesita robótica al recibir respuesta — cada mascota tiene su "voz"
    static void onReply(PetType pet) {
        switch (pet) {
            case PET_KRAKEN: {  // gargareo profundo de kraken
                uint32_t freqs[] = {180, 220, 180, 260, 180};
                for (int i = 0; i < 5; i++) { tone(freqs[i], 50); delay(10); }
                break;
            }
            case PET_EYE: {     // serie de tonos escaneando
                for (int f = 800; f <= 1400; f += 150) { tone(f, 40); }
                break;
            }
            case PET_CRTBOT: {  // melodía 8-bit corta
                uint32_t notes[] = {523, 659, 784, 659, 523};
                for (int i = 0; i < 5; i++) { tone(notes[i], 80); delay(20); }
                break;
            }
            case PET_DRONE: {   // vibrato mecánico
                for (int i = 0; i < 4; i++) {
                    tone(300 + (i % 2) * 40, 60);
                }
                break;
            }
            case PET_BLOB: {    // burbujas random
                uint32_t b[] = {400, 600, 350, 700, 500};
                for (int i = 0; i < 5; i++) { tone(b[i], 45); delay(15); }
                break;
            }
        }
    }

    // Error / sin conexión
    static void onError() {
        tone(300, 150); delay(50); tone(200, 200);
    }
}

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
AudioManager g_audio;

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

// ── Audio / PTT (Push To Talk) ───────────────────────────────────────────────
static uint32_t g_spaceHeldAt  = 0;     // millis() cuando se apretó space
static bool     g_spaceWasHeld = false; // true si superó el umbral
static const uint32_t PTT_THRESHOLD = 500; // ms para activar grabación

// ── Menú audio (Fn+M) ────────────────────────────────────────────────────────
static bool g_menuOpen    = false;
static int  g_menuCursor  = 0;
static int  g_voiceIndex  = 0;

// ── Menú brain (Fn+B) ────────────────────────────────────────────────────────
static bool g_brainMenuOpen    = false;
static int  g_brainMenuCursor  = 0;

// ── Menú mascota (Fn+P) ──────────────────────────────────────────────────────
static bool g_petMenuOpen   = false;
static int  g_petMenuCursor = 0;

// ── WebServer toggle ──────────────────────────────────────────────────────────
static bool g_webEnabled = true;

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
    const int chatBot  = 112;  // deja zona input de 113 a 135
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

    // ── INPUT: Font0 size 2 — 16px alto, top-left coords (sin baseline offset)
    // Separador en y=112, texto desde y=115, bottom en y=131 — entra en 135px
    canvas.setFont(&fonts::Font0);
    canvas.drawLine(0, 112, W, 112, 0x1082);
    canvas.setTextSize(2);
    canvas.setTextColor(accent);
    canvas.setCursor(2, 115);
    canvas.print(">");

    const int inputCharW = 12;
    int maxInputChars    = (W - 18) / inputCharW;
    String visible = g_inputBuffer;
    if ((int)visible.length() > maxInputChars)
        visible = visible.substring(visible.length() - maxInputChars);
    canvas.setTextColor(TFT_WHITE);
    canvas.setCursor(16, 115);
    canvas.print(visible);

    // Cursor parpadeante
    if ((millis() / 500) % 2 == 0) {
        int cx = 16 + (int)visible.length() * inputCharW;
        canvas.fillRect(cx, 114, 10, 16, accent);
    }

    // ── Indicador REC ────────────────────────────────────────────────────────
    if (g_audio.isRecording()) {
        canvas.setFont(&fonts::Font0);
        // Punto rojo parpadeante + "REC"
        if ((millis() / 300) % 2 == 0) {
            canvas.fillCircle(W - 16, 8, 5, TFT_RED);
        }
        canvas.setTextSize(1);
        canvas.setTextColor(TFT_RED);
        canvas.setCursor(W - 38, 4);
        canvas.print("REC");
    } else if (g_spaceWasHeld || (g_spaceHeldAt > 0 && millis() - g_spaceHeldAt > 200)) {
        // Indicador "mantené..." mientras se acumula el tiempo
        canvas.setFont(&fonts::Font0);
        canvas.setTextSize(1);
        canvas.setTextColor(KRAKEN_GLOW);
        canvas.setCursor(W - 50, 4);
        canvas.print("hold..");
    }

    canvas.pushSprite(0, 0);
}

// ── Menú audio ────────────────────────────────────────────────────────────────
void drawMenu() {
    if (!g_canvasReady) return;
    const int W = M5Cardputer.Display.width();
    const int H = M5Cardputer.Display.height();

    canvas.fillScreen(0x0841);
    canvas.setFont(&fonts::Font0);

    // Título
    canvas.setTextSize(2);
    canvas.setTextColor(KRAKEN_RED);
    canvas.setCursor(4, 4);
    canvas.print("AUDIO MENU");

    canvas.drawLine(0, 22, W, 22, 0x1082);

    // Opciones
    const char* labels[] = {
        "TTS",
        "Volumen +",
        "Volumen -",
        "Voz",
        "Cerrar"
    };
    const int N = 5;

    for (int i = 0; i < N; i++) {
        bool sel = (i == g_menuCursor);
        canvas.setTextSize(1);
        canvas.setTextColor(sel ? 0x0841 : TFT_WHITE);
        if (sel) canvas.fillRect(0, 26 + i * 18, W, 16, KRAKEN_RED);
        canvas.setCursor(6, 28 + i * 18);

        if (i == 0) {
            // TTS con estado
            String line = String("TTS: ") + (g_audio.getTTS() ? "ON " : "OFF");
            canvas.print(line);
        } else if (i == 3) {
            // Voz actual
            String line = String("Voz: ") + AudioManager::VOICES[g_voiceIndex];
            canvas.print(line);
        } else if (i == 1 || i == 2) {
            String line = String(labels[i]) + " (" + g_audio.getVolume() + ")";
            canvas.print(line);
        } else {
            canvas.print(labels[i]);
        }
    }

    canvas.pushSprite(0, 0);
}

// ── Menú brain ────────────────────────────────────────────────────────────────
void drawBrainMenu() {
    if (!g_canvasReady) return;
    const int W = M5Cardputer.Display.width();
    const int H = M5Cardputer.Display.height();

    canvas.fillScreen(0x0841);

    // Título — Font0 size 1
    canvas.setFont(&fonts::Font0);
    canvas.setTextSize(1);
    canvas.setTextColor(KRAKEN_RED);
    canvas.setCursor(4, 4);
    canvas.print("[ BRAIN ]");
    canvas.drawLine(0, 14, W, 14, 0x1082);

    // Opciones: los 3 providers + cerrar — Font0 size 1 → 8px alto, 6px/char
    const char* labels[] = { "OpenAI", "n8n Webhook", "Claude Gateway", "Cerrar" };
    const int N   = 4;
    const int rowH = 24;  // 135px / 4 filas aprox
    BrainProvider cur = g_cfg.brain.provider;

    canvas.setFont(&fonts::Font0);
    canvas.setTextSize(1);
    for (int i = 0; i < N; i++) {
        bool sel    = (i == g_brainMenuCursor);
        bool active = (i < 3 && (BrainProvider)i == cur);
        int  rowY   = 16 + i * rowH;
        if (sel) canvas.fillRect(0, rowY, W, rowH - 2, KRAKEN_RED);
        uint16_t fg = sel ? 0x0841 : (active ? KRAKEN_GLOW : TFT_WHITE);
        canvas.setTextColor(fg);
        canvas.setCursor(6, rowY + 8);
        String line = String(labels[i]);
        if (active) line += "  <activo>";
        canvas.print(line);
    }

    // Hint
    canvas.setTextColor(0x4A69);
    canvas.setCursor(2, H - 8);
    canvas.print("Fn+;/Fn+.  Enter  Del");

    canvas.pushSprite(0, 0);
}

// ── Menú mascota ─────────────────────────────────────────────────────────────
void drawPetMenu() {
    if (!g_canvasReady) return;
    const int W = M5Cardputer.Display.width();
    const int H = M5Cardputer.Display.height();

    canvas.fillScreen(0x0841);
    canvas.setFont(&fonts::Font0);
    canvas.setTextSize(1);
    canvas.setTextColor(KRAKEN_RED);
    canvas.setCursor(4, 4);
    canvas.print("[ PET ]");
    canvas.drawLine(0, 14, W, 14, 0x1082);

    const char* names[] = { "Kraken", "Eye", "CRTBot", "Drone", "Blob" };
    const int N   = 5;
    const int rowH = 20;
    PetType cur = g_cfg.pet.type;

    for (int i = 0; i < N; i++) {
        bool sel    = (i == g_petMenuCursor);
        bool active = ((PetType)i == cur);
        int  rowY   = 16 + i * rowH;
        if (sel) canvas.fillRect(0, rowY, W, rowH - 2, KRAKEN_RED);
        uint16_t fg = sel ? 0x0841 : (active ? KRAKEN_GLOW : TFT_WHITE);
        canvas.setTextColor(fg);
        canvas.setCursor(6, rowY + 6);
        String line = String(names[i]);
        if (active) line += "  <activa>";
        canvas.print(line);
    }

    canvas.setTextColor(0x4A69);
    canvas.setCursor(2, H - 8);
    canvas.print("Fn+;/Fn+.  Enter  Del");
    canvas.pushSprite(0, 0);
}

// ── Send message ──────────────────────────────────────────────────────────────
void sendMessage(const String& text) {
    if (text.isEmpty() || g_waitingReply) return;
    addHistory("user", text);
    g_waitingReply = true;
    g_statusLine   = "pensando...";
    g_pet.setState(PET_THINKING);
    Sound::onSend(g_cfg.pet.type);   // beep al enviar
    String* msgPtr = new String(text);
    xQueueSend(chatQueue, &msgPtr, 0);
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
    auto cfg = M5.config();
    cfg.serial_baudrate = 115200;
    M5Cardputer.begin(cfg, true);
    M5Cardputer.Display.setRotation(1);
    delay(100);

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

    // Audio pendiente
    // g_audio.begin(g_cfg.audio.ttsVolume);
    // g_audio.setTTS(g_cfg.audio.ttsEnabled);

    g_web.onSave([&]() {
        g_brain.setConfig(g_cfg.brain);
        g_pet.setType(g_cfg.pet.type);
        g_statusLine = g_brain.hasCredentials() ? "online" : "sin brain";
        g_audio.setVolume(g_cfg.audio.ttsVolume);
    });
    g_web.begin(g_cfg);
    g_webReady = true;

    // Speaker
    M5Cardputer.Speaker.setVolume(g_cfg.audio.ttsVolume * 255 / 100);
    M5Cardputer.Speaker.begin();

    Serial.printf("[KRAKBOT] Ready! IP: %s\n", WifiManager::localIP().c_str());
    g_pet.setState(PET_IDLE);
    drawUI();
}

// ── Loop ──────────────────────────────────────────────────────────────────────
void loop() {
    M5Cardputer.update();
    if (g_webEnabled) g_web.handle();

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
            Sound::onError();
        } else {
            addHistory("bot", resp);
            g_statusLine = "online";
            Sound::onReply(g_cfg.pet.type);   // vocesita al recibir
            g_pet.setState(PET_TALKING);
            g_idleAt = millis() + 3500;
            // TTS: reproducir respuesta si está activado
            if (g_audio.getTTS()) {
                g_statusLine = "hablando...";
                drawUI();
                g_audio.speak(resp, g_cfg.audio.openaiKey,
                              AudioManager::VOICES[g_voiceIndex]);
            }
        }
    }

    // ── Grabación continua si está activa ───────────────────────────────────
    if (g_audio.isRecording()) {
        g_audio.updateRecording();
    }

    // ── Teclado ──────────────────────────────────────────────────────────────
    if (M5Cardputer.Keyboard.isChange()) {
        Keyboard_Class::KeysState ks = M5Cardputer.Keyboard.keysState();

        // ── Menú mascota ─────────────────────────────────────────────────────
        if (g_petMenuOpen) {
            if (M5Cardputer.Keyboard.isPressed()) {
                if (ks.fn) {
                    for (char c : ks.word) {
                        if (c == ';') g_petMenuCursor = (g_petMenuCursor - 1 + 5) % 5;
                        if (c == '.') g_petMenuCursor = (g_petMenuCursor + 1) % 5;
                    }
                } else {
                    if (ks.enter) {
                        g_cfg.pet.type = (PetType)g_petMenuCursor;
                        g_pet.setType(g_cfg.pet.type);
                        Storage::savePet(g_cfg.pet);
                        // Personalidad según mascota
                        switch (g_cfg.pet.type) {
                            case PET_KRAKEN:  strlcpy(g_cfg.pet.name, "Kraken",  sizeof(g_cfg.pet.name)); break;
                            case PET_EYE:     strlcpy(g_cfg.pet.name, "Eye",     sizeof(g_cfg.pet.name)); break;
                            case PET_CRTBOT:  strlcpy(g_cfg.pet.name, "CRTBot",  sizeof(g_cfg.pet.name)); break;
                            case PET_DRONE:   strlcpy(g_cfg.pet.name, "Drone",   sizeof(g_cfg.pet.name)); break;
                            case PET_BLOB:    strlcpy(g_cfg.pet.name, "Blob",    sizeof(g_cfg.pet.name)); break;
                        }
                        addHistory("bot", String("Hola! Soy ") + g_cfg.pet.name + ".");
                        g_petMenuOpen = false;
                    }
                    if (ks.del) g_petMenuOpen = false;
                }
            }
        // ── Menú brain ───────────────────────────────────────────────────────
        } else if (g_brainMenuOpen) {
            if (M5Cardputer.Keyboard.isPressed()) {
                bool changed = false;
                if (ks.fn) {
                    // Fn+; sube | Fn+. baja
                    for (char c : ks.word) {
                        if (c == ';') { g_brainMenuCursor = (g_brainMenuCursor - 1 + 4) % 4; changed = true; }
                        if (c == '.') { g_brainMenuCursor = (g_brainMenuCursor + 1) % 4; changed = true; }
                    }
                } else {
                    // Enter confirma | Del cierra
                    if (ks.enter) {
                        switch (g_brainMenuCursor) {
                            case 0:
                                g_cfg.brain.provider = BRAIN_OPENAI;
                                addHistory("bot", "Brain: OpenAI");
                                g_brainMenuOpen = false;
                                break;
                            case 1:
                                g_cfg.brain.provider = BRAIN_N8N;
                                addHistory("bot", "Brain: n8n Webhook");
                                g_brainMenuOpen = false;
                                break;
                            case 2:
                                g_cfg.brain.provider = BRAIN_CLAUDE;
                                addHistory("bot", "Brain: Claude Gateway");
                                g_brainMenuOpen = false;
                                break;
                            case 3:
                                g_brainMenuOpen = false;
                                break;
                        }
                        if (!g_brainMenuOpen) {
                            Storage::saveBrain(g_cfg.brain);
                            g_brain.setConfig(g_cfg.brain);
                            g_statusLine = g_brain.hasCredentials() ? "online" : "sin creds";
                        }
                        changed = true;
                    }
                    if (ks.del) { g_brainMenuOpen = false; changed = true; }
                }
                (void)changed;  // el loop principal redibuja siempre
            }
        // ── Menú audio ───────────────────────────────────────────────────────
        } else if (g_menuOpen) {
            if (M5Cardputer.Keyboard.isPressed()) {
                bool changed = false;
                for (char c : ks.word) {
                    if (c == '\n' || c == '\r') {
                        // Confirmar opción
                        switch (g_menuCursor) {
                            case 0: // Toggle TTS
                                g_audio.setTTS(!g_audio.getTTS());
                                g_cfg.audio.ttsEnabled = g_audio.getTTS();
                                Storage::saveAll(g_cfg);
                                break;
                            case 1: // Vol+
                                g_audio.setVolume(g_audio.getVolume() + 10);
                                g_cfg.audio.ttsVolume = g_audio.getVolume();
                                Storage::saveAll(g_cfg);
                                break;
                            case 2: // Vol-
                                g_audio.setVolume(g_audio.getVolume() - 10);
                                g_cfg.audio.ttsVolume = g_audio.getVolume();
                                Storage::saveAll(g_cfg);
                                break;
                            case 3: // Cambiar voz
                                g_voiceIndex = (g_voiceIndex + 1) % AudioManager::VOICE_COUNT;
                                // Guardar en config
                                strlcpy(g_cfg.audio.ttsVoice,
                                        AudioManager::VOICES[g_voiceIndex],
                                        sizeof(g_cfg.audio.ttsVoice));
                                Storage::saveAll(g_cfg);
                                break;
                            case 4: // Cerrar
                                g_menuOpen = false;
                                break;
                        }
                        changed = true;
                    }
                }
                // Navegación arriba/abajo
                if (ks.fn) {
                    for (char c : ks.word) {
                        if (c == ';') { g_menuCursor = (g_menuCursor - 1 + 5) % 5; changed = true; }
                        if (c == '.') { g_menuCursor = (g_menuCursor + 1) % 5; changed = true; }
                    }
                }
                if (ks.del) { g_menuOpen = false; changed = true; }
                if (changed) drawMenu();
            }
        } else {
            // ── Modo normal ──────────────────────────────────────────────────
            bool typed = false;

            if (M5Cardputer.Keyboard.isPressed()) {

                // Fn+M audio | Fn+B brain | Fn+P pet | Fn+W webserver | Fn+;/. scroll
                if (ks.fn) {
                    for (char c : ks.word) {
                        if (c == 'm' || c == 'M') {
                            g_menuOpen   = true;
                            g_menuCursor = 0;
                        }
                        if (c == 'b' || c == 'B') {
                            g_brainMenuOpen   = true;
                            g_brainMenuCursor = 0;
                        }
                        if (c == 'p' || c == 'P') {
                            g_petMenuOpen   = true;
                            g_petMenuCursor = (int)g_cfg.pet.type;
                        }
                        if (c == 'w' || c == 'W') {
                            g_webEnabled = !g_webEnabled;
                            if (g_webEnabled) {
                                g_web.begin(g_cfg);
                                g_statusLine = "web ON";
                            } else {
                                g_web.end();
                                g_statusLine = "web OFF";
                            }
                            g_idleAt = millis() + 2000;
                        }
                        if (c == 's' || c == 'S') {
                            g_soundEnabled = !g_soundEnabled;
                            M5Cardputer.Speaker.setVolume(g_soundEnabled ? g_cfg.audio.ttsVolume * 255 / 100 : 0);
                            g_statusLine = g_soundEnabled ? "sound ON" : "sound OFF";
                            g_idleAt = millis() + 2000;
                        }
                        if (c == ';') {
                            g_scrollOffset++;
                            if (g_scrollOffset > g_historyCount * 5) g_scrollOffset = g_historyCount * 5;
                        }
                        if (c == '.') {
                            if (g_scrollOffset > 0) g_scrollOffset--;
                        }
                    }
                } else {
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
                        // Space: detectar hold (no agregar al buffer todavía)
                        if (c == ' ') {
                            if (g_spaceHeldAt == 0) g_spaceHeldAt = millis();
                        } else if (c >= 32 && g_inputBuffer.length() < 200) {
                            g_inputBuffer += c;
                            typed = true;
                        }
                    }
                }
            } else {
                // Tecla soltada — detectar fin de space hold
                if (g_spaceHeldAt > 0) {
                    uint32_t held = millis() - g_spaceHeldAt;
                    if (g_audio.isRecording()) {
                        // Soltar space → parar grabación y transcribir
                        g_audio.stopRecording();
                        g_statusLine = "transcribiendo...";
                        g_pet.setState(PET_THINKING);
                        drawUI();
                        String text = g_audio.transcribe(g_cfg.audio.openaiKey);
                        if (!text.isEmpty()) {
                            addHistory("user", "[voz] " + text);
                            sendMessage(text);
                        } else {
                            g_statusLine = "no se entendio";
                            addHistory("bot", "No pude entender el audio. " + g_audio.lastError());
                            g_pet.setState(PET_ERROR);
                            g_idleAt = millis() + 3000;
                        }
                    } else if (held < PTT_THRESHOLD) {
                        // Tap corto → agregar espacio al buffer
                        if (g_inputBuffer.length() < 200) {
                            g_inputBuffer += ' ';
                            typed = true;
                        }
                    }
                    g_spaceHeldAt  = 0;
                    g_spaceWasHeld = false;
                }
            }

            // Activar grabación si space se mantiene > umbral
            if (g_spaceHeldAt > 0 && !g_audio.isRecording() &&
                millis() - g_spaceHeldAt >= PTT_THRESHOLD && !g_waitingReply) {
                g_spaceWasHeld = true;
                g_audio.startRecording();
                g_statusLine = "grabando...";
                g_pet.setState(PET_LISTENING);
            }

            if (typed) {
                g_statusLine = "escribiendo...";
                g_pet.setState(PET_LISTENING);
            }
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
    if      (g_petMenuOpen)   drawPetMenu();
    else if (g_brainMenuOpen) drawBrainMenu();
    else if (g_menuOpen)      drawMenu();
    else                      drawUI();
    delay(33);
}
