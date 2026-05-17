#pragma once
#include <Arduino.h>

// ─────────────────────────────────────────────
// KRAKBOT — Config structs & constants
// ─────────────────────────────────────────────

#define KRAKBOT_VERSION     "0.1.0"
#define KRAKBOT_AP_SSID     "KRAKBOT-SETUP"
#define KRAKBOT_AP_PASS     ""
#define CONFIG_PATH_WIFI    "/config/wifi.json"
#define CONFIG_PATH_AUDIO   "/config/audio.json"
#define CONFIG_PATH_BRAIN   "/config/brains.json"
#define CONFIG_PATH_PET     "/config/pet.json"

// ─── Brain providers ───────────────────────
enum BrainProvider {
    BRAIN_OPENAI = 0,
    BRAIN_N8N    = 1,
    BRAIN_CLAUDE = 2
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
    bool  whisperEnabled = false;
    bool  ttsEnabled     = false;
    char  ttsVoice[32]   = "nova";
    float ttsSpeed       = 1.0f;
    int   ttsVolume      = 80;
    char  openaiKey[256] = "";
};

struct BrainConfig {
    BrainProvider provider = BRAIN_OPENAI;
    char openaiKey[256]      = "";
    char openaiModel[32]     = "gpt-4o-mini";
    char n8nWebhookUrl[256]  = "";
    char n8nAuthToken[128]   = "";
    char claudeGatewayUrl[256] = "";
    char claudeAuthToken[128]  = "";
};

struct PetConfig {
    PetType type  = PET_KRAKEN;
    char name[32] = "Kraken";
};

struct AppConfig {
    WifiConfig  wifi;
    AudioConfig audio;
    BrainConfig brain;
    PetConfig   pet;
};

// ─── Global compartido entre módulos ────────
extern AppConfig   g_cfg;
extern String      g_pendingMessage;
extern bool        g_waitingReply;
