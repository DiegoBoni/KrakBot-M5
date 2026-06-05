#pragma once
#include <Arduino.h>

// ─────────────────────────────────────────────
// KRAKBOT — Config structs & constants
// ─────────────────────────────────────────────

#define KRAKBOT_VERSION     "0.2.0"
#define KRAKBOT_AP_SSID     "KRAKBOT-SETUP"
#define KRAKBOT_AP_PASS     ""
#define CONFIG_PATH_WIFI    "/config/wifi.json"
#define CONFIG_PATH_AUDIO   "/config/audio.json"
#define CONFIG_PATH_BRAIN   "/config/brain_%d.json"
#define CONFIG_PATH_PET     "/config/pet.json"
#define CONFIG_PATH_SOUL    "/config/soul.json"

// ─── Brain providers ───────────────────────
enum BrainProvider {
    BRAIN_OPENAI = 0,
    BRAIN_N8N    = 1
};

// ─── Pet types ─────────────────────────────
enum PetType {
    PET_KRAKEN  = 0,
    PET_EYE     = 1,
    PET_CRTBOT  = 2,
    PET_DRONE   = 3,
    PET_BLOB    = 4
};

// ─── Pet animation states ──────────────────
enum PetState {
    PET_IDLE      = 0,
    PET_BLINK     = 1,
    PET_LISTENING = 2,
    PET_THINKING  = 3,
    PET_TALKING   = 4,
    PET_SLEEPING  = 5,
    PET_ERROR     = 6,
    PET_OFFLINE   = 7
};

// ─── Config structs ─────────────────────────
struct WifiConfig {
    char ssid[64]     = "";
    char password[64] = "";
    bool configured   = false;
};

struct AudioConfig {
    bool ttsEnabled     = false;
    char ttsVoice[32]   = "nova";
    int  ttsVolume      = 70;
    char openaiKey[256] = "";
};

struct BrainConfig {
    BrainProvider provider   = BRAIN_OPENAI;
    char openaiKey[256]      = "";
    char openaiModel[32]     = "gpt-4o-mini";
    char n8nWebhookUrl[256]  = "";
    char n8nAuthToken[128]   = "";
};

struct SoulConfig {
    bool enabled[5] = {false, false, false, false, false};
};

struct PetConfig {
    PetType type  = PET_KRAKEN;
    char name[32] = "Kraken";
};

struct AppConfig {
    WifiConfig  wifi;
    AudioConfig audio;
    BrainConfig brains[5];
    PetConfig   pet;
    SoulConfig  soul;
};

// ─── Chat history ────────────────────────────
struct ChatEntry { String role; String text; };
constexpr int MAX_HISTORY    = 15;
constexpr int MAX_HISTORY_FS = 25;

// ─── Global compartido entre módulos ────────
extern AppConfig   g_cfg;
extern String      g_pendingMessage;
extern bool        g_waitingReply;
