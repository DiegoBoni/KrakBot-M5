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
#include "AudioManager.h"

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

// Helper: brain de la mascota activa
inline BrainConfig& activeBrain() { return g_cfg.brains[(int)g_cfg.pet.type]; }

M5Canvas canvas(&M5Cardputer.Display);
bool g_canvasReady = false;

Pet          g_pet;
BrainManager g_brain;
WebConfig    g_web;
AudioManager g_audio;

// ── Chat history (ChatEntry y MAX_HISTORY definidos en config.h) ─────────────
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

// ── Menú config unificado (Fn+C) ─────────────────────────────────────────────
enum ConfigScreen { CFG_NONE, CFG_MAIN, CFG_PET, CFG_BRAIN, CFG_AUDIO };
static ConfigScreen g_cfgScreen = CFG_NONE;
static int          g_cfgCursor = 0;
static int          g_voiceIndex = 0;

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
            // Sincronizar brain + soul con la mascota activa justo antes de chatear
            g_brain.setConfig(activeBrain());
            g_brain.setSoul(&g_cfg.soul, (int)g_cfg.pet.type);
            String resp = g_brain.chat(*msgPtr);
            delete msgPtr;
            String* replyPtr = new String(resp);
            xQueueSend(replyQueue, &replyPtr, portMAX_DELAY);
        }
    }
}

// ── Boot screen animado ───────────────────────────────────────────────────────
// Boot con mascota ASCII centrada, mensajes secuenciales y barra sinusoidal
static void drawBootScreen(const String& msg, int frame = 0, const char* petAscii = "(*)" ) {
    if (!g_canvasReady) return;
    const int W = M5Cardputer.Display.width();   // 240
    const int H = M5Cardputer.Display.height();  // 135

    canvas.fillScreen(0x0841);
    canvas.setFont(&fonts::Font0);

    // ── Mascota ASCII arriba centrada — size 2 (12px/char)
    canvas.setTextSize(2);
    canvas.setTextColor(KRAKEN_RED);
    int petW = strlen(petAscii) * 12;
    canvas.setCursor((W - petW) / 2, 6);
    canvas.print(petAscii);

    // ── "KRAKBOT" — size 2
    canvas.setTextSize(2);
    canvas.setTextColor(KRAKEN_RED);
    canvas.setCursor((W - 84) / 2, 30);
    canvas.print("KRAKBOT");

    // ── "companion OS" — size 1
    canvas.setTextSize(1);
    canvas.setTextColor(KRAKEN_DIM);
    canvas.setCursor((W - 72) / 2, 52);
    canvas.print("companion OS");

    // ── Separador
    canvas.drawLine(20, 64, W - 20, 64, 0x1082);

    // ── Mensaje de estado — size 1, centrado
    canvas.setTextSize(1);
    canvas.setTextColor(KRAKEN_GLOW);
    int mw = msg.length() * 6;
    canvas.setCursor((W - mw) / 2, 72);
    canvas.print(msg);

    // ── Barra de progreso sinusoidal
    int barW = W - 48;
    int barX = 24;
    int barY = 92;
    canvas.drawRect(barX, barY, barW, 5, 0x1082);
    float t = (sinf(frame * 0.22f) + 1.0f) * 0.5f;
    canvas.fillRect(barX, barY, (int)(t * barW), 5, KRAKEN_RED);

    // ── Version chiquita abajo
    canvas.setTextSize(1);
    canvas.setTextColor(0x2945);
    canvas.setCursor(4, H - 10);
    canvas.print("v" KRAKBOT_VERSION);

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

    // ── CHAT: Font0 size 1 (6x8px) — más compacto y predecible ─────────────
    canvas.setFont(&fonts::Font0);
    canvas.setTextSize(2);
    const int chatTop  = 50;
    const int chatBot  = 112;
    const int lineH    = 17;                          // Font0 size2 = 16px + 1px gap
    const int charW    = 12;                          // Font0 size2 = 12px/char exacto
    const int maxLines = (chatBot - chatTop) / lineH; // = 3-4 líneas visibles
    const int chatW    = W - 10;

    static String    allLines[120];
    static uint16_t  allColors[120];
    int totalLines = 0;

    for (int e = 0; e < g_historyCount && totalLines < 116; e++) {
        bool isUser   = (g_history[e].role == "user");
        uint16_t col  = isUser ? 0xFD20 : TFT_WHITE;  // amarillo user, blanco bot
        String prefix = isUser ? ">" : " ";
        String full   = prefix + toASCII(g_history[e].text);
        String wrapped[24];
        int n = wrapText(full, charW, chatW, wrapped, 24);
        for (int i = 0; i < n && totalLines < 116; i++) {
            allLines[totalLines]  = wrapped[i];
            allColors[totalLines] = col;
            totalLines++;
        }
    }

    // Puntos de "pensando" al final
    if (g_waitingReply && totalLines < 118) {
        uint8_t d = (millis() / 350) % 4;
        String dots = " ";
        for (uint8_t i = 0; i <= d; i++) dots += ".";
        allLines[totalLines]  = dots;
        allColors[totalLines] = KRAKEN_GLOW;
        totalLines++;
    }

    // Auto-scroll: mostrar siempre las últimas líneas salvo que el usuario scrolleó
    int maxScroll = (totalLines > maxLines) ? (totalLines - maxLines) : 0;
    if (g_scrollOffset > maxScroll) g_scrollOffset = maxScroll;

    int startLine = totalLines - maxLines - g_scrollOffset;
    if (startLine < 0) startLine = 0;
    int endLine = startLine + maxLines;
    if (endLine > totalLines) endLine = totalLines;

    int cy = chatTop + 14;  // Font0 size2 baseline
    for (int i = startLine; i < endLine; i++) {
        canvas.setTextColor(allColors[i]);
        canvas.setCursor(3, cy);
        canvas.print(allLines[i]);
        cy += lineH;
    }

    // Indicador scroll (hay mensajes más arriba)
    if (g_scrollOffset > 0) {
        canvas.setTextColor(accent);
        canvas.setCursor(W - 7, chatTop);
        canvas.print("^");
    }
    // Indicador hay más abajo
    if (startLine > 0 && g_scrollOffset == 0) {
        // nada, auto-scroll está al fondo
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

// ── Menú config unificado ─────────────────────────────────────────────────────
void drawConfigMenu() {
    if (!g_canvasReady) return;
    const int W = M5Cardputer.Display.width();
    const int H = M5Cardputer.Display.height();

    canvas.fillScreen(0x0841);
    canvas.setFont(&fonts::Font0);

    // ── Cabecera
    canvas.setTextSize(2);
    canvas.setTextColor(KRAKEN_RED);
    canvas.setCursor(4, 4);
    switch (g_cfgScreen) {
        case CFG_MAIN:  canvas.print("[ CONFIG ]");  break;
        case CFG_PET:   canvas.print("[ MASCOTA ]"); break;
        case CFG_BRAIN: canvas.print("[ BRAIN — "); canvas.print(g_cfg.pet.name); canvas.print(" ]"); break;
        case CFG_AUDIO: canvas.print("[ AUDIO ]");   break;
        default: break;
    }
    canvas.drawLine(0, 20, W, 20, 0x1082);

    // ── Filas según pantalla activa
    canvas.setTextSize(1);
    const int rowH = 17;
    const int rowY0 = 23;

    auto drawRow = [&](int i, const String& label, bool active = false) {
        bool sel = (i == g_cfgCursor);
        int ry = rowY0 + i * rowH;
        if (sel) canvas.fillRect(0, ry, W, rowH - 2, KRAKEN_RED);
        uint16_t fg = sel ? (uint16_t)0x0841 : (active ? (uint16_t)KRAKEN_GLOW : (uint16_t)TFT_WHITE);
        canvas.setTextColor(fg);
        canvas.setCursor(6, ry + 6);
        canvas.print(label);
    };

    if (g_cfgScreen == CFG_MAIN) {
        drawRow(0, String("Mascota:  ") + g_cfg.pet.name);
        drawRow(1, String("Brain:    ") + (activeBrain().provider == BRAIN_N8N ? "N8N" : "OpenAI"));
        drawRow(2, String("Audio:    ") + (g_audio.getTTS() ? "TTS ON" : "TTS OFF"));
        drawRow(3, String("Web:      ") + (g_webEnabled ? "ON" : "OFF"));
        drawRow(4, String("Sonido:   ") + (g_soundEnabled ? "ON" : "OFF"));
        drawRow(5, "Cerrar");

    } else if (g_cfgScreen == CFG_PET) {
        const char* names[] = { "Kraken", "Eye", "CRTBot", "Drone", "Blob" };
        for (int i = 0; i < 5; i++)
            drawRow(i, String(names[i]), (PetType)i == g_cfg.pet.type);
        drawRow(5, "< Volver");

    } else if (g_cfgScreen == CFG_BRAIN) {
        const char* provs[] = { "OpenAI", "N8N Webhook" };
        for (int i = 0; i < 2; i++)
            drawRow(i, String(provs[i]), (BrainProvider)i == activeBrain().provider);
        drawRow(2, "< Volver");

    } else if (g_cfgScreen == CFG_AUDIO) {
        drawRow(0, String("TTS:      ") + (g_audio.getTTS() ? "ON" : "OFF"));
        drawRow(1, String("Volumen:  ") + g_audio.getVolume());
        drawRow(2, String("Voz:      ") + AudioManager::VOICES[g_voiceIndex]);
        drawRow(3, "< Volver");
    }

    // Hint navegación
    canvas.setTextSize(1);
    canvas.setTextColor(0x4A69);
    canvas.setCursor(2, H - 8);
    canvas.print(";  .  Enter  Del");

    canvas.pushSprite(0, 0);
}

// ── Send message ──────────────────────────────────────────────────────────────
// ── Switch pet — guarda historial actual, carga el de la nueva mascota ───────
void switchPet(PetType newPet) {
    // Guardar historial actual antes de cambiar
    Storage::saveHistory(g_cfg.pet.type, g_history, g_historyCount);

    // Sincronizar nombre desde type (evita desync)
    switch (newPet) {
        case PET_KRAKEN: strlcpy(g_cfg.pet.name, "Kraken", sizeof(g_cfg.pet.name)); break;
        case PET_EYE:    strlcpy(g_cfg.pet.name, "Eye",    sizeof(g_cfg.pet.name)); break;
        case PET_CRTBOT: strlcpy(g_cfg.pet.name, "CRTBot", sizeof(g_cfg.pet.name)); break;
        case PET_DRONE:  strlcpy(g_cfg.pet.name, "Drone",  sizeof(g_cfg.pet.name)); break;
        case PET_BLOB:   strlcpy(g_cfg.pet.name, "Blob",   sizeof(g_cfg.pet.name)); break;
    }
    g_cfg.pet.type = newPet;
    g_pet.setType(newPet);
    Storage::savePet(g_cfg.pet);

    // Cargar brain de la nueva mascota
    g_brain.setConfig(activeBrain()); g_brain.setSoul(&g_cfg.soul, (int)g_cfg.pet.type);
    g_statusLine = g_brain.hasCredentials() ? "online" : "sin brain";

    // Cargar historial de la nueva mascota
    g_historyCount = Storage::loadHistory(newPet, g_history, MAX_HISTORY);
    g_scrollOffset = 0;

    if (g_historyCount == 0)
        addHistory("bot", String("Hola! Soy ") + g_cfg.pet.name + ".");
    else
        addHistory("bot", String("De vuelta con ") + g_cfg.pet.name + ".");
}

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

    // ── ASCII de mascota para boot (se actualiza tras cargar config)
    const char* bootPetAscii = "(*)";

    // ── Boot fase 1: Storage
    for (int f = 0; f < 15; f++) { drawBootScreen("Loading config...", f, bootPetAscii); delay(40); }

    Storage::begin();
    Storage::loadAll(g_cfg);

    // Sanity check: provider inválido → reset cada brain
    for (int i = 0; i < 5; i++) {
        if ((int)g_cfg.brains[i].provider > 2) {
            g_cfg.brains[i] = BrainConfig();
            Storage::saveBrain(g_cfg.brains[i], i);
        }
    }

    // ── Fix mascota: siempre derivar nombre desde type (evita desync nombre/imagen)
    switch (g_cfg.pet.type) {
        case PET_KRAKEN: strlcpy(g_cfg.pet.name, "Kraken", sizeof(g_cfg.pet.name)); bootPetAscii = "(*)"; break;
        case PET_EYE:    strlcpy(g_cfg.pet.name, "Eye",    sizeof(g_cfg.pet.name)); bootPetAscii = "(o)"; break;
        case PET_CRTBOT: strlcpy(g_cfg.pet.name, "CRTBot", sizeof(g_cfg.pet.name)); bootPetAscii = "[R]"; break;
        case PET_DRONE:  strlcpy(g_cfg.pet.name, "Drone",  sizeof(g_cfg.pet.name)); bootPetAscii = " ^ "; break;
        case PET_BLOB:   strlcpy(g_cfg.pet.name, "Blob",   sizeof(g_cfg.pet.name)); bootPetAscii = "~~~"; break;
    }
    g_pet.setType(g_cfg.pet.type);

    // ── Migración: si brain_0 está vacío pero existe el viejo brains.json, migrar
    if (strlen(activeBrain().openaiKey) == 0 && LittleFS.exists("/config/brains.json")) {
        Serial.println("[KRAKBOT] Migrando brains.json → brain_N.json");
        File f = LittleFS.open("/config/brains.json", "r");
        if (f) {
            JsonDocument doc;
            if (deserializeJson(doc, f) == DeserializationError::Ok) {
                BrainConfig migrated;
                migrated.provider = (BrainProvider)(doc["provider"] | 0);
                if ((int)migrated.provider > 2) migrated.provider = BRAIN_OPENAI;
                strlcpy(migrated.openaiKey,        doc["openaiKey"]        | "", sizeof(migrated.openaiKey));
                strlcpy(migrated.openaiModel,      doc["openaiModel"]      | "gpt-4o-mini", sizeof(migrated.openaiModel));
                strlcpy(migrated.n8nWebhookUrl,    doc["n8nWebhookUrl"]    | "", sizeof(migrated.n8nWebhookUrl));
                strlcpy(migrated.n8nAuthToken,     doc["n8nAuthToken"]     | "", sizeof(migrated.n8nAuthToken));
                for (int i = 0; i < 5; i++) {
                    g_cfg.brains[i] = migrated;
                    Storage::saveBrain(g_cfg.brains[i], i);
                }
                Serial.println("[KRAKBOT] Migración OK");
            }
            f.close();
        }
    }

    g_brain.setConfig(activeBrain()); g_brain.setSoul(&g_cfg.soul, (int)g_cfg.pet.type);

    // Cargar historial de la mascota activa
    g_historyCount = Storage::loadHistory(g_cfg.pet.type, g_history, MAX_HISTORY);
    g_scrollOffset = 0;

    // ── Boot fase 2: Brain
    for (int f = 0; f < 15; f++) { drawBootScreen("Initializing brain...", f, bootPetAscii); delay(40); }

    chatQueue  = xQueueCreate(1, sizeof(String*));
    replyQueue = xQueueCreate(1, sizeof(String*));
    xTaskCreatePinnedToCore(chatTask, "chatTask", 8192, nullptr, 1, nullptr, 0);

    // ── Boot fase 3: WiFi
    for (int f = 0; f < 10; f++) { drawBootScreen("Connecting...", f, bootPetAscii); delay(40); }

    bool wifiOk = WifiManager::connectToSaved(g_cfg.wifi, 8000);

    if (!wifiOk) {
        WifiManager::startAP();
        g_statusLine = "AP: 192.168.4.1";
        if (g_historyCount == 0)
            addHistory("bot", "Conectate a KRAKBOT-SETUP y entra a 192.168.4.1 para configurar.");
    } else {
        if (g_brain.hasCredentials()) {
            g_statusLine = "online";
            if (g_historyCount == 0)
                addHistory("bot", String("Hola! Soy ") + g_cfg.pet.name + ". En que te ayudo?");
        } else {
            g_statusLine = "sin brain";
            if (g_historyCount == 0)
                addHistory("bot", "Conectado! Configura el Brain en " + WifiManager::localIP());
        }
    }

    // Audio
    g_audio.begin(g_cfg.audio.ttsVolume);
    g_audio.setTTS(g_cfg.audio.ttsEnabled);

    g_web.onSave([&]() {
        g_brain.setConfig(activeBrain()); g_brain.setSoul(&g_cfg.soul, (int)g_cfg.pet.type);
        // Sincronizar nombre desde type al guardar desde web
        switch (g_cfg.pet.type) {
            case PET_KRAKEN: strlcpy(g_cfg.pet.name, "Kraken", sizeof(g_cfg.pet.name)); break;
            case PET_EYE:    strlcpy(g_cfg.pet.name, "Eye",    sizeof(g_cfg.pet.name)); break;
            case PET_CRTBOT: strlcpy(g_cfg.pet.name, "CRTBot", sizeof(g_cfg.pet.name)); break;
            case PET_DRONE:  strlcpy(g_cfg.pet.name, "Drone",  sizeof(g_cfg.pet.name)); break;
            case PET_BLOB:   strlcpy(g_cfg.pet.name, "Blob",   sizeof(g_cfg.pet.name)); break;
        }
        g_pet.setType(g_cfg.pet.type);
        g_statusLine = g_brain.hasCredentials() ? "online" : "sin brain";
        g_audio.setVolume(g_cfg.audio.ttsVolume);
        g_audio.setTTS(g_cfg.audio.ttsEnabled);
    });
    g_web.setRefs(g_history, &g_historyCount, &g_waitingReply);
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
            Storage::saveHistory(g_cfg.pet.type, g_history, g_historyCount);
            g_statusLine = "online";
            Sound::onReply(g_cfg.pet.type);   // vocesita al recibir
            g_pet.setState(PET_TALKING);
            g_idleAt = millis() + 3500;
            // TTS: reproducir respuesta si está activado
            if (g_audio.getTTS()) {
                g_statusLine = "hablando...";
                drawUI();
                // Usar audio.openaiKey si está seteado, sino fallback a brain.openaiKey
                const char* ttsKey = (strlen(g_cfg.audio.openaiKey) > 0)
                                     ? g_cfg.audio.openaiKey
                                     : activeBrain().openaiKey;
                g_audio.speak(resp, ttsKey,
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

        // ── Menú config unificado ─────────────────────────────────────────────
        if (g_cfgScreen != CFG_NONE) {
            if (M5Cardputer.Keyboard.isPressed()) {
                // Navegación directa con ; y .
                for (char c : ks.word) {
                    if (c == ';') { g_cfgCursor = max(0, g_cfgCursor - 1); }
                    if (c == '.') { g_cfgCursor++; }
                }
                // Enter — confirmar
                if (ks.enter) {
                    if (g_cfgScreen == CFG_MAIN) {
                        switch (g_cfgCursor) {
                            case 0: g_cfgScreen = CFG_PET;   g_cfgCursor = (int)g_cfg.pet.type; break;
                            case 1: g_cfgScreen = CFG_BRAIN; g_cfgCursor = (int)activeBrain().provider; break;
                            case 2: g_cfgScreen = CFG_AUDIO; g_cfgCursor = 0; break;
                            case 3: // Web toggle
                                g_webEnabled = !g_webEnabled;
                                if (g_webEnabled) { g_web.begin(g_cfg); } else { g_web.end(); }
                                g_statusLine = g_webEnabled ? "web ON" : "web OFF";
                                g_idleAt = millis() + 2000;
                                break;
                            case 4: // Sonido toggle
                                g_soundEnabled = !g_soundEnabled;
                                M5Cardputer.Speaker.setVolume(g_soundEnabled ? g_cfg.audio.ttsVolume * 255 / 100 : 0);
                                break;
                            case 5: g_cfgScreen = CFG_NONE; break;
                        }
                    } else if (g_cfgScreen == CFG_PET) {
                        if (g_cfgCursor <= 4) {
                            switchPet((PetType)g_cfgCursor);
                        }
                        g_cfgScreen = CFG_MAIN; g_cfgCursor = 0;
                    } else if (g_cfgScreen == CFG_BRAIN) {
                        if (g_cfgCursor <= 1) {
                            activeBrain().provider = (BrainProvider)g_cfgCursor;
                            Storage::saveBrain(activeBrain(), (int)g_cfg.pet.type);
                            g_brain.setConfig(activeBrain()); g_brain.setSoul(&g_cfg.soul, (int)g_cfg.pet.type);
                            g_statusLine = g_brain.hasCredentials() ? "online" : "sin creds";
                            const char* names[] = {"OpenAI","N8N"};
                            addHistory("bot", String("Brain: ") + names[g_cfgCursor]);
                        }
                        g_cfgScreen = CFG_MAIN; g_cfgCursor = 1;
                    } else if (g_cfgScreen == CFG_AUDIO) {
                        switch (g_cfgCursor) {
                            case 0: // Toggle TTS
                                g_audio.setTTS(!g_audio.getTTS());
                                g_cfg.audio.ttsEnabled = g_audio.getTTS();
                                Storage::saveAudio(g_cfg.audio);
                                break;
                            case 1: // Vol+
                                g_audio.setVolume(min(100, g_audio.getVolume() + 10));
                                g_cfg.audio.ttsVolume = g_audio.getVolume();
                                Storage::saveAudio(g_cfg.audio);
                                break;
                            case 2: // Voz (ciclar)
                                g_voiceIndex = (g_voiceIndex + 1) % AudioManager::VOICE_COUNT;
                                strlcpy(g_cfg.audio.ttsVoice, AudioManager::VOICES[g_voiceIndex], sizeof(g_cfg.audio.ttsVoice));
                                Storage::saveAudio(g_cfg.audio);
                                break;
                            case 3: // Volver
                                g_cfgScreen = CFG_MAIN; g_cfgCursor = 2;
                                break;
                        }
                    }
                }
                // Del — volver un nivel
                else if (ks.del) {
                    if (g_cfgScreen == CFG_MAIN) { g_cfgScreen = CFG_NONE; }
                    else { g_cfgScreen = CFG_MAIN; g_cfgCursor = 0; }
                }

                // Clampear cursor según pantalla
                int maxCursor = 5;
                if (g_cfgScreen == CFG_PET)   maxCursor = 5;
                if (g_cfgScreen == CFG_BRAIN)  maxCursor = 2;
                if (g_cfgScreen == CFG_AUDIO)  maxCursor = 3;
                if (g_cfgCursor > maxCursor) g_cfgCursor = maxCursor;
            }

        } else {
            // ── Modo normal ──────────────────────────────────────────────────
            bool typed = false;

            if (M5Cardputer.Keyboard.isPressed()) {
                if (ks.fn) {
                    for (char c : ks.word) {
                        // Fn+C → abrir config unificado
                        if (c == 'c' || c == 'C') {
                            g_cfgScreen = CFG_MAIN;
                            g_cfgCursor = 0;
                        }
                        // Scroll chat
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
                        if (c == ' ') {
                            if (g_spaceHeldAt == 0) g_spaceHeldAt = millis();
                        } else if (c >= 32 && g_inputBuffer.length() < 200) {
                            g_inputBuffer += c;
                            typed = true;
                        }
                    }
                }
            } else {
                // Tecla soltada
                if (g_spaceHeldAt > 0) {
                    uint32_t held = millis() - g_spaceHeldAt;
                    if (g_audio.isRecording()) {
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
                        if (g_inputBuffer.length() < 200) {
                            g_inputBuffer += ' ';
                            typed = true;
                        }
                    }
                    g_spaceHeldAt  = 0;
                    g_spaceWasHeld = false;
                }
            }

            // Activar grabación PTT
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
    if (g_cfgScreen != CFG_NONE) drawConfigMenu();
    else                         drawUI();
    delay(33);
}
