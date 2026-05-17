#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <driver/i2s.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ─── ES8311 I2C address ───────────────────────────────────────────────────────
#define ES8311_ADDR       0x18
#define I2C_SDA           8
#define I2C_SCL           9

// ─── I2S pins (ES8311 en Cardputer ADV) ──────────────────────────────────────
#define I2S_SCLK          41
#define I2S_LRCK          43
#define I2S_DOUT          42   // ESP32 → ES8311 (playback)
#define I2S_DIN           46   // ES8311 → ESP32 (grabación)

// ─── Audio config ─────────────────────────────────────────────────────────────
#define SAMPLE_RATE       16000
#define BITS_PER_SAMPLE   16
#define CHANNELS          1
#define REC_MAX_SECONDS   10
#define REC_BUFFER_SIZE   (SAMPLE_RATE * (BITS_PER_SAMPLE/8) * CHANNELS * REC_MAX_SECONDS)

// ─── ES8311 registers ────────────────────────────────────────────────────────
#define ES8311_REG_RESET         0x00
#define ES8311_REG_CLKMGR1       0x01
#define ES8311_REG_CLKMGR2       0x02
#define ES8311_REG_CLKMGR3       0x03
#define ES8311_REG_CLKMGR4       0x04
#define ES8311_REG_CLKMGR5       0x05
#define ES8311_REG_CLKMGR6       0x06
#define ES8311_REG_CLKMGR7       0x07
#define ES8311_REG_CLKMGR8       0x08
#define ES8311_REG_SDPIN         0x09
#define ES8311_REG_SDPOUT        0x0A
#define ES8311_REG_SYS1          0x0D
#define ES8311_REG_SYS2          0x0E
#define ES8311_REG_SYS3          0x0F
#define ES8311_REG_SYS4          0x10
#define ES8311_REG_SYS5          0x11
#define ES8311_REG_SYS6          0x12
#define ES8311_REG_SYS7          0x13
#define ES8311_REG_SYS8          0x14
#define ES8311_REG_ADC1          0x15
#define ES8311_REG_ADC2          0x16
#define ES8311_REG_ADC_MUTE      0x17
#define ES8311_REG_ADC_VOL       0x18
#define ES8311_REG_DAC1          0x31
#define ES8311_REG_DAC2          0x32
#define ES8311_REG_DAC_MUTE      0x33
#define ES8311_REG_DAC_VOL       0x37
#define ES8311_REG_GPIO          0x44
#define ES8311_REG_CHD1          0xFD
#define ES8311_REG_CHD2          0xFE

class AudioManager {
public:
    // ── Init ─────────────────────────────────────────────────────────────────
    bool begin(int volume = 70) {
        _volume = volume;
        _ttsEnabled = false;
        _recording  = false;
        _recBuffer  = nullptr;
        _recBytes   = 0;

        // M5Cardputer ya inicializó el ES8311 — no lo reseteamos
        // Solo arrancamos el driver I2S y ajustamos volumen
        initI2S();
        setVolume(_volume);
        Serial.println("[AUDIO] I2S OK");
        return true;
    }

    // ── TTS toggle ───────────────────────────────────────────────────────────
    void setTTS(bool enabled)   { _ttsEnabled = enabled; }
    bool getTTS() const         { return _ttsEnabled; }
    void setVolume(int vol) {
        _volume = constrain(vol, 0, 100);
        if (_i2sReady) {
            uint8_t reg = (uint8_t)(_volume * 255 / 100);
            writeReg(ES8311_REG_DAC_VOL, reg);
        }
    }
    int  getVolume() const      { return _volume; }

    // ── Grabación ─────────────────────────────────────────────────────────────
    bool startRecording() {
        if (!_i2sReady || _recording) return false;
        // Alocar buffer en PSRAM si está disponible
        if (psramFound()) {
            _recBuffer = (uint8_t*)ps_malloc(REC_BUFFER_SIZE);
        } else {
            _recBuffer = (uint8_t*)malloc(REC_BUFFER_SIZE);
        }
        if (!_recBuffer) { Serial.println("[AUDIO] No memory for rec"); return false; }
        _recBytes   = 0;
        _recording  = true;
        _recStart   = millis();
        Serial.println("[AUDIO] Recording started");
        return true;
    }

    // Llamar en cada loop mientras _recording == true
    void updateRecording() {
        if (!_recording) return;
        if (millis() - _recStart > REC_MAX_SECONDS * 1000UL) {
            Serial.println("[AUDIO] Max rec time reached");
            // No paramos aquí, el caller llama stopRecording()
            return;
        }
        uint8_t tmp[512];
        size_t bytesRead = 0;
        i2s_read(I2S_NUM_0, tmp, sizeof(tmp), &bytesRead, 0);
        if (bytesRead > 0 && _recBytes + bytesRead <= REC_BUFFER_SIZE) {
            memcpy(_recBuffer + _recBytes, tmp, bytesRead);
            _recBytes += bytesRead;
        }
    }

    void stopRecording() {
        _recording = false;
        Serial.printf("[AUDIO] Recording stopped: %u bytes\n", _recBytes);
    }

    bool isRecording()     const { return _recording; }
    bool hasRecording()    const { return _recBuffer != nullptr && _recBytes > 0; }
    uint32_t recDuration() const { return millis() - _recStart; }

    // ── Transcripción via Whisper ──────────────────────────────────────────────
    String transcribe(const char* apiKey) {
        if (!hasRecording()) { _lastError = "Sin grabacion"; return ""; }
        if (!apiKey || strlen(apiKey) == 0) { _lastError = "Sin API key"; return ""; }

        Serial.printf("[AUDIO] Transcribing %u bytes...\n", _recBytes);

        // 1) Construir WAV completo en memoria (header + PCM)
        uint32_t wavSize = _recBytes + 44;
        uint8_t* wavBuf  = psramFound() ? (uint8_t*)ps_malloc(wavSize)
                                        : (uint8_t*)malloc(wavSize);
        if (!wavBuf) { _lastError = "No mem WAV"; freeRecBuffer(); return ""; }
        buildWAVHeader(wavBuf, _recBytes, SAMPLE_RATE, CHANNELS, BITS_PER_SAMPLE);
        memcpy(wavBuf + 44, _recBuffer, _recBytes);
        freeRecBuffer();  // ya copiado, liberamos

        // 2) Construir multipart body
        String boundary = "kb42boundary";
        String partHead =
            "--" + boundary + "\r\n"
            "Content-Disposition: form-data; name=\"model\"\r\n\r\n"
            "whisper-1\r\n"
            "--" + boundary + "\r\n"
            "Content-Disposition: form-data; name=\"language\"\r\n\r\n"
            "es\r\n"
            "--" + boundary + "\r\n"
            "Content-Disposition: form-data; name=\"file\"; filename=\"audio.wav\"\r\n"
            "Content-Type: audio/wav\r\n\r\n";
        String partTail = "\r\n--" + boundary + "--\r\n";
        uint32_t totalLen = partHead.length() + wavSize + partTail.length();

        uint8_t* body = psramFound() ? (uint8_t*)ps_malloc(totalLen)
                                     : (uint8_t*)malloc(totalLen);
        if (!body) { free(wavBuf); _lastError = "No mem POST"; return ""; }

        uint32_t off = 0;
        memcpy(body + off, partHead.c_str(), partHead.length()); off += partHead.length();
        memcpy(body + off, wavBuf, wavSize);                      off += wavSize;
        memcpy(body + off, partTail.c_str(), partTail.length());
        free(wavBuf);

        // 3) POST a Whisper
        HTTPClient http;
        http.begin("https://api.openai.com/v1/audio/transcriptions");
        http.addHeader("Authorization", String("Bearer ") + apiKey);
        http.addHeader("Content-Type", "multipart/form-data; boundary=" + boundary);
        http.setTimeout(30000);

        int code = http.POST(body, totalLen);
        free(body);

        if (code != 200) {
            _lastError = "Whisper HTTP " + String(code);
            http.end();
            return "";
        }

        String resp = http.getString();
        http.end();

        // 4) Parsear JSON {"text":"..."}
        JsonDocument doc;
        if (deserializeJson(doc, resp) != DeserializationError::Ok) {
            _lastError = "JSON parse error";
            return "";
        }
        String text = doc["text"] | "";
        text.trim();
        Serial.println("[AUDIO] Transcript: " + text);
        return text;
    }

    // ── TTS via OpenAI ────────────────────────────────────────────────────────
    // Llama a /v1/audio/speech, descarga MP3, lo decodifica y reproduce.
    // voice: "alloy","echo","fable","onyx","nova","shimmer"
    bool speak(const String& text, const char* apiKey, const char* voice = "nova") {
        if (!_ttsEnabled || !_i2sReady) return false;
        if (!apiKey || strlen(apiKey) == 0) return false;
        if (text.isEmpty()) return false;

        Serial.println("[AUDIO] TTS: " + text.substring(0, 40));

        HTTPClient http;
        http.begin("https://api.openai.com/v1/audio/speech");
        http.addHeader("Authorization", String("Bearer ") + apiKey);
        http.addHeader("Content-Type", "application/json");
        http.setTimeout(30000);

        JsonDocument doc;
        doc["model"] = "tts-1";
        doc["input"] = text;
        doc["voice"] = voice;
        doc["response_format"] = "wav";  // WAV es más fácil de reproducir sin decoder
        String body; serializeJson(doc, body);

        int code = http.POST(body);
        if (code != 200) {
            _lastError = "TTS HTTP " + String(code);
            http.end();
            return false;
        }

        int len = http.getSize();
        Serial.printf("[AUDIO] TTS response: %d bytes\n", len);

        // Stream directo desde HTTP al I2S, parseando el header WAV
        WiFiClient* stream = http.getStreamPtr();
        if (!stream) { http.end(); return false; }

        // Leer y descartar header WAV (44 bytes)
        uint8_t wavHeader[44];
        int headerRead = 0;
        uint32_t timeout = millis() + 5000;
        while (headerRead < 44 && millis() < timeout) {
            if (stream->available()) {
                wavHeader[headerRead++] = stream->read();
            }
        }

        // Stream PCM al I2S
        uint8_t buf[512];
        uint32_t playTimeout = millis() + 30000;
        while ((stream->available() || http.connected()) && millis() < playTimeout) {
            int avail = stream->available();
            if (avail > 0) {
                int toRead = min(avail, (int)sizeof(buf));
                int got = stream->readBytes(buf, toRead);
                size_t written = 0;
                i2s_write(I2S_NUM_0, buf, got, &written, portMAX_DELAY);
            } else {
                delay(1);
            }
        }

        http.end();
        Serial.println("[AUDIO] TTS done");
        return true;
    }

    String lastError() const { return _lastError; }

    // ── Voices disponibles ───────────────────────────────────────────────────
    static constexpr const char* VOICES[] = {"nova","alloy","echo","fable","onyx","shimmer"};
    static constexpr int VOICE_COUNT = 6;

private:
    bool     _ttsEnabled  = false;
    bool     _recording   = false;
    bool     _i2sReady    = false;
    int      _volume      = 70;
    uint8_t* _recBuffer   = nullptr;
    uint32_t _recBytes    = 0;
    uint32_t _recStart    = 0;
    String   _lastError;

    // ── ES8311 I2C ────────────────────────────────────────────────────────────
    void writeReg(uint8_t reg, uint8_t val) {
        Wire.beginTransmission(ES8311_ADDR);
        Wire.write(reg);
        Wire.write(val);
        Wire.endTransmission();
    }

    uint8_t readReg(uint8_t reg) {
        Wire.beginTransmission(ES8311_ADDR);
        Wire.write(reg);
        Wire.endTransmission(false);
        Wire.requestFrom((uint8_t)ES8311_ADDR, (uint8_t)1);
        return Wire.available() ? Wire.read() : 0;
    }

    bool initES8311() {
        // Verificar que el chip responde
        Wire.beginTransmission(ES8311_ADDR);
        if (Wire.endTransmission() != 0) return false;

        // Reset
        writeReg(ES8311_REG_RESET, 0x1F);
        delay(10);
        writeReg(ES8311_REG_RESET, 0x00);
        delay(10);

        // Clock: MCLK interno, 16kHz sample rate
        // Con MCLK=12.288MHz, LRCK=16kHz: MCLK/LRCK=768
        writeReg(ES8311_REG_CLKMGR1, 0x30);  // MCLK from SCLK, not inverted
        writeReg(ES8311_REG_CLKMGR2, 0x00);  // MCLK divider
        writeReg(ES8311_REG_CLKMGR3, 0x10);  // SCLK divider
        writeReg(ES8311_REG_CLKMGR4, 0x10);  // LRCK H
        writeReg(ES8311_REG_CLKMGR5, 0x00);  // LRCK L
        writeReg(ES8311_REG_CLKMGR6, 0x03);  // BCLK divider
        writeReg(ES8311_REG_CLKMGR7, 0x00);
        writeReg(ES8311_REG_CLKMGR8, 0xFF);

        // I2S format: 16bit, I2S standard
        writeReg(ES8311_REG_SDPIN,  0x00);   // I2S, 16bit, slave
        writeReg(ES8311_REG_SDPOUT, 0x00);   // I2S, 16bit, slave

        // Sistema
        writeReg(ES8311_REG_SYS1, 0x3F);
        writeReg(ES8311_REG_SYS2, 0x00);
        writeReg(ES8311_REG_SYS3, 0x00);
        writeReg(ES8311_REG_SYS4, 0x00);
        writeReg(ES8311_REG_SYS5, 0x01);
        writeReg(ES8311_REG_SYS6, 0x00);
        writeReg(ES8311_REG_SYS7, 0x7C);
        writeReg(ES8311_REG_SYS8, 0x00);     // No bypass

        // ADC (micrófono)
        writeReg(ES8311_REG_ADC1,     0x28); // ADC gain
        writeReg(ES8311_REG_ADC2,     0x00);
        writeReg(ES8311_REG_ADC_MUTE, 0x00); // unmute
        writeReg(ES8311_REG_ADC_VOL,  0xBF); // volumen ADC

        // DAC (speaker)
        writeReg(ES8311_REG_DAC1,     0x00);
        writeReg(ES8311_REG_DAC_MUTE, 0x00); // unmute
        writeReg(ES8311_REG_DAC_VOL,  0xBF); // ~75%

        // GPIO (PA enable si aplica)
        writeReg(ES8311_REG_GPIO, 0x00);

        delay(20);
        return true;
    }

    // ── I2S ──────────────────────────────────────────────────────────────────
    void initI2S() {
        i2s_config_t cfg = {
            .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX),
            .sample_rate          = SAMPLE_RATE,
            .bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT,
            .channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT,
            .communication_format = I2S_COMM_FORMAT_STAND_I2S,
            .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
            .dma_buf_count        = 8,
            .dma_buf_len          = 512,
            .use_apll             = false,  // ESP32-S3 no tiene APLL
            .tx_desc_auto_clear   = true,
            .fixed_mclk           = 0
        };

        i2s_pin_config_t pins = {
            .mck_io_num   = I2S_PIN_NO_CHANGE,
            .bck_io_num   = I2S_SCLK,
            .ws_io_num    = I2S_LRCK,
            .data_out_num = I2S_DOUT,
            .data_in_num  = I2S_DIN
        };

        esp_err_t err = i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr);
        if (err != ESP_OK) {
            Serial.printf("[AUDIO] i2s_driver_install failed: %d\n", err);
            _i2sReady = false;
            return;
        }
        i2s_set_pin(I2S_NUM_0, &pins);
        i2s_zero_dma_buffer(I2S_NUM_0);
        _i2sReady = true;
    }

    // ── WAV header builder ───────────────────────────────────────────────────
    void buildWAVHeader(uint8_t* buf, uint32_t dataBytes,
                        uint32_t sampleRate, uint8_t channels, uint8_t bitsPerSample) {
        uint32_t byteRate   = sampleRate * channels * bitsPerSample / 8;
        uint16_t blockAlign = channels * bitsPerSample / 8;
        uint32_t chunkSize  = dataBytes + 36;

        auto w32 = [&](int off, uint32_t v) {
            buf[off]=v; buf[off+1]=v>>8; buf[off+2]=v>>16; buf[off+3]=v>>24;
        };
        auto w16 = [&](int off, uint16_t v) {
            buf[off]=v; buf[off+1]=v>>8;
        };

        memcpy(buf,    "RIFF", 4);  w32(4,  chunkSize);
        memcpy(buf+8,  "WAVE", 4);
        memcpy(buf+12, "fmt ", 4);  w32(16, 16);
        w16(20, 1);                 // PCM
        w16(22, channels);          w32(24, sampleRate);
        w32(28, byteRate);          w16(32, blockAlign);
        w16(34, bitsPerSample);
        memcpy(buf+36, "data", 4);  w32(40, dataBytes);
    }

    void freeRecBuffer() {
        if (_recBuffer) { free(_recBuffer); _recBuffer = nullptr; _recBytes = 0; }
    }
};
