#pragma once
#include <Arduino.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <M5Cardputer.h>

// ─────────────────────────────────────────────────────────────────────────────
//  AudioManager — TTS via OpenAI usando M5Cardputer.Speaker.playRaw()
//  Sin I2S directo. Usa la misma API que CardputerMP3Player.
//  Sample rate OpenAI TTS WAV: 24000Hz, 16bit, mono
// ─────────────────────────────────────────────────────────────────────────────

#define TTS_SAMPLE_RATE   24000
#define TTS_CHUNK_SIZE    1024   // bytes por chunk al leer el stream
#define TTS_MAX_WAV_SIZE  (512 * 1024)  // 512KB máx en PSRAM

class AudioManager {
public:
    bool begin(int volume = 70) {
        _volume     = volume;
        _ttsEnabled = false;
        Serial.println("[AUDIO] AudioManager ready (M5Speaker mode)");
        return true;
    }

    void setTTS(bool enabled) { _ttsEnabled = enabled; }
    bool getTTS() const       { return _ttsEnabled; }

    void setVolume(int vol) {
        _volume = constrain(vol, 0, 100);
        // Mapeamos 0-100 a 0-255 para M5Speaker
        M5Cardputer.Speaker.setVolume((uint8_t)(_volume * 255 / 100));
    }
    int getVolume() const { return _volume; }

    // Stubs de grabación (no implementados en esta versión)
    bool startRecording()    { return false; }
    void updateRecording()   {}
    void stopRecording()     {}
    bool isRecording() const { return false; }
    String transcribe(const char*) { return ""; }

    // ── TTS via OpenAI ────────────────────────────────────────────────────────
    // Descarga WAV de OpenAI TTS, parsea el header, reproduce con playRaw.
    // voice: "alloy","echo","fable","onyx","nova","shimmer"
    bool speak(const String& text, const char* apiKey, const char* voice = "nova") {
        if (!_ttsEnabled) return false;
        if (!apiKey || strlen(apiKey) == 0) { _lastError = "Sin API key"; return false; }
        if (text.isEmpty()) return false;

        Serial.println("[AUDIO] TTS: " + text.substring(0, 60));

        // 1. POST a OpenAI TTS
        HTTPClient http;
        http.begin("https://api.openai.com/v1/audio/speech");
        http.addHeader("Authorization", String("Bearer ") + apiKey);
        http.addHeader("Content-Type", "application/json");
        http.setTimeout(30000);

        JsonDocument doc;
        doc["model"]           = "tts-1";
        doc["input"]           = text;
        doc["voice"]           = voice;
        doc["response_format"] = "wav";
        String body;
        serializeJson(doc, body);

        int code = http.POST(body);
        if (code != 200) {
            _lastError = "TTS HTTP " + String(code) + ": " + http.getString().substring(0, 80);
            http.end();
            Serial.println("[AUDIO] " + _lastError);
            return false;
        }

        int contentLen = http.getSize();
        Serial.printf("[AUDIO] TTS WAV: %d bytes\n", contentLen);

        // 2. Descargar WAV completo en PSRAM (o heap si no hay PSRAM)
        int bufSize = (contentLen > 0 && contentLen <= TTS_MAX_WAV_SIZE)
                      ? contentLen : TTS_MAX_WAV_SIZE;

        uint8_t* wavBuf = nullptr;
        if (psramFound()) {
            wavBuf = (uint8_t*)ps_malloc(bufSize);
        } else {
            wavBuf = (uint8_t*)malloc(bufSize);
        }

        if (!wavBuf) {
            _lastError = "Sin memoria para TTS";
            http.end();
            Serial.println("[AUDIO] " + _lastError);
            return false;
        }

        // Leer stream completo
        WiFiClient* stream = http.getStreamPtr();
        int totalRead = 0;
        uint32_t timeout = millis() + 20000;
        while (millis() < timeout && totalRead < bufSize) {
            int avail = stream->available();
            if (avail > 0) {
                int toRead = min(avail, (int)(bufSize - totalRead));
                toRead = min(toRead, TTS_CHUNK_SIZE);
                int got = stream->readBytes(wavBuf + totalRead, toRead);
                totalRead += got;
            } else if (!http.connected()) {
                break;
            } else {
                delay(1);
            }
        }
        http.end();

        Serial.printf("[AUDIO] Downloaded %d bytes\n", totalRead);

        if (totalRead < 44) {
            _lastError = "WAV demasiado corto";
            free(wavBuf);
            return false;
        }

        // 3. Parsear header WAV para encontrar el PCM data
        // Buscamos el chunk "data"
        int dataOffset = -1;
        int dataSize   = 0;
        int sampleRate = TTS_SAMPLE_RATE;
        int channels   = 1;

        // Parsear los chunks RIFF
        if (wavBuf[0]=='R' && wavBuf[1]=='I' && wavBuf[2]=='F' && wavBuf[3]=='F' &&
            wavBuf[8]=='W' && wavBuf[9]=='A' && wavBuf[10]=='V' && wavBuf[11]=='E') {

            int pos = 12;
            while (pos + 8 <= totalRead) {
                char chunkId[5] = {0};
                memcpy(chunkId, wavBuf + pos, 4);
                uint32_t chunkSize = wavBuf[pos+4] | (wavBuf[pos+5]<<8) |
                                     (wavBuf[pos+6]<<16) | (wavBuf[pos+7]<<24);
                pos += 8;

                if (strcmp(chunkId, "fmt ") == 0 && chunkSize >= 16) {
                    sampleRate = wavBuf[pos+4] | (wavBuf[pos+5]<<8) |
                                 (wavBuf[pos+6]<<16) | (wavBuf[pos+7]<<24);
                    channels   = wavBuf[pos+2] | (wavBuf[pos+3]<<8);
                    Serial.printf("[AUDIO] WAV: %dHz, %dch\n", sampleRate, channels);
                } else if (strcmp(chunkId, "data") == 0) {
                    dataOffset = pos;
                    dataSize   = min((int)chunkSize, totalRead - pos);
                    break;
                }
                pos += (int)chunkSize;
                if (chunkSize & 1) pos++;  // word-align
            }
        }

        if (dataOffset < 0 || dataSize <= 0) {
            _lastError = "WAV data chunk no encontrado";
            free(wavBuf);
            Serial.println("[AUDIO] " + _lastError);
            return false;
        }

        // 4. Reproducir PCM con M5Speaker.playRaw()
        // PSRAM no es accesible por DMA — copiar PCM a heap normal primero
        int      nSamples = dataSize / 2;  // 16bit = 2 bytes/sample
        bool     stereo   = (channels == 2);

        int16_t* pcm = (int16_t*)malloc(dataSize);
        if (!pcm) {
            _lastError = "Sin heap para PCM";
            free(wavBuf);
            Serial.println("[AUDIO] " + _lastError);
            return false;
        }
        memcpy(pcm, wavBuf + dataOffset, dataSize);
        free(wavBuf);  // liberar PSRAM

        // Ajustar volumen por software
        float gain = (float)_volume / 100.0f;
        for (int i = 0; i < nSamples; i++) {
            pcm[i] = (int16_t)(pcm[i] * gain);
        }

        Serial.printf("[AUDIO] Playing %d samples @ %dHz\n", nSamples, sampleRate);
        M5Cardputer.Speaker.stop();
        M5Cardputer.Speaker.playRaw(pcm, nSamples, sampleRate, stereo, 1, 0);

        // Esperar que termine — M5.update() necesario para que el speaker procese
        uint32_t playTimeout = millis() + 30000;
        while (M5Cardputer.Speaker.isPlaying() && millis() < playTimeout) {
            M5Cardputer.update();
            delay(10);
        }
        M5Cardputer.Speaker.stop();

        free(pcm);
        Serial.println("[AUDIO] TTS done");
        return true;
    }

    String lastError() const { return _lastError; }

    static constexpr const char* VOICES[]   = {"nova","alloy","echo","fable","onyx","shimmer"};
    static constexpr int         VOICE_COUNT = 6;

private:
    bool   _ttsEnabled = false;
    int    _volume     = 70;
    String _lastError;
};
