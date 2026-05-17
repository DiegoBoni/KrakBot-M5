#pragma once
#include <M5Cardputer.h>
#include "config.h"

// ─────────────────────────────────────────────
// Pet — mascota animada con estados
// Dibuja directamente sobre M5Cardputer.Display
// usando un LGFX_Sprite pasado desde afuera
// ─────────────────────────────────────────────

class Pet {
public:
    Pet() {}

    void setCanvas(LGFX_Sprite* canvas) { _canvas = canvas; }

    void setType(PetType type) {
        _type  = type;
        _frame = 0;
        _state = PET_IDLE;
        _blinkTimer = 0;
    }

    void setState(PetState state) {
        if (_state == state) return;
        _state = state;
        _frame = 0;
        _lastFrame = millis();
    }

    PetState getState() const { return _state; }

    void update() {
        uint32_t now = millis();
        if (now - _lastFrame >= frameInterval()) {
            _frame++;
            _lastFrame = now;
            if (_state == PET_IDLE) {
                _blinkTimer++;
                if (_blinkTimer > 5) {
                    _blinkTimer = 0;
                    _blinking = !_blinking;
                }
            } else {
                _blinking = false;
            }
        }
    }

    void draw(int x, int y) {
        if (!_canvas) return;
        switch (_type) {
            case PET_KRAKEN: drawKraken(x, y); break;
            case PET_EYE:    drawEye(x, y);    break;
            case PET_CRTBOT: drawCRTBot(x, y); break;
            case PET_DRONE:  drawDrone(x, y);  break;
            case PET_BLOB:   drawBlob(x, y);   break;
        }
    }

    const char* getName() const {
        const char* names[] = {"Kraken","Eye","CRT-Bot","Drone","Blob"};
        return names[(int)_type];
    }

    uint16_t getAccentColor() const {
        // cyan, magenta, verde, naranja, lila
        const uint16_t colors[] = {0x07FF, 0xF81F, 0x07E0, 0xFD20, 0xD69A};
        return colors[(int)_type];
    }

private:
    LGFX_Sprite* _canvas   = nullptr;
    PetType      _type     = PET_KRAKEN;
    PetState     _state    = PET_IDLE;
    int          _frame    = 0;
    uint32_t     _lastFrame = 0;
    int          _blinkTimer = 0;
    bool         _blinking   = false;
    static constexpr float PET_SCALE = 1.45f;

    int sp(int v) const { return (int)lroundf(v * PET_SCALE); }
    int sx(int x, int dx) const { return x + sp(dx); }
    int sy(int y, int dy) const { return y + sp(dy); }
    int sr(int r) const { return std::max(1, sp(r)); }

    uint32_t frameInterval() const {
        switch (_state) {
            case PET_THINKING:  return 200;
            case PET_TALKING:   return 150;
            case PET_LISTENING: return 300;
            case PET_SLEEPING:  return 1200;
            case PET_ERROR:     return 100;
            default:            return 600;
        }
    }

    // ── Kraken ───────────────────────────────
    void drawKraken(int x, int y) {
        uint16_t col = getAccentColor();
        int bobY = y + sp((int)(sinf(_frame * 0.4f) * 2));

        // Cuerpo
        _canvas->fillEllipse(sx(x, 16), sy(bobY, 14), sr(14), sr(11), col);

        // Ojos
        bool eyesClosed = _blinking || (_state == PET_SLEEPING);
        if (!eyesClosed) {
            _canvas->fillCircle(sx(x, 11), sy(bobY, 11), sr(3), TFT_WHITE);
            _canvas->fillCircle(sx(x, 21), sy(bobY, 11), sr(3), TFT_WHITE);
            _canvas->fillCircle(sx(x, 12), sy(bobY, 11), sr(1), TFT_BLACK);
            _canvas->fillCircle(sx(x, 22), sy(bobY, 11), sr(1), TFT_BLACK);
        } else {
            _canvas->drawLine(sx(x, 8), sy(bobY, 11), sx(x, 14), sy(bobY, 11), TFT_WHITE);
            _canvas->drawLine(sx(x, 18), sy(bobY, 11), sx(x, 24), sy(bobY, 11), TFT_WHITE);
        }

        // Boca
        if (_state == PET_TALKING && _frame % 2 == 0) {
            _canvas->fillEllipse(sx(x, 16), sy(bobY, 18), sr(4), sr(3), TFT_WHITE);
        } else {
            _canvas->drawLine(sx(x, 12), sy(bobY, 18), sx(x, 20), sy(bobY, 18), TFT_WHITE);
        }

        // Tentáculos
        const int offsets[] = {-11, -7, -4, -1, 2, 5, 8, 12};
        for (int i = 0; i < 8; i++) {
            int tx   = sx(x, 16 + offsets[i]);
            int ty   = sy(bobY, 23);
            int wave = sp((int)(sinf((_frame + i) * 0.5f) * 3));
            int endY = ty + sp(5 + (i % 3));
            _canvas->drawLine(tx, ty, tx + wave, endY, col);
            _canvas->fillCircle(tx + wave, endY, sr(1), col);
        }

        // Thinking dots
        if (_state == PET_THINKING) {
            for (int d = 0; d < 3; d++) {
                if (_frame % 3 == d)
                    _canvas->fillCircle(sx(x, 10 + d * 6), sy(bobY, -4), sr(2), TFT_YELLOW);
            }
        }
        if (_state == PET_ERROR) {
            _canvas->setTextColor(TFT_RED);
            _canvas->drawString("!", sx(x, 14), sy(bobY, -6));
        }
    }

    // ── Eye ──────────────────────────────────
    void drawEye(int x, int y) {
        uint16_t col = getAccentColor();
        int bobY = y + sp((int)(sinf(_frame * 0.3f) * 2));
        bool closed = _blinking || (_state == PET_SLEEPING);

        if (!closed) {
            _canvas->fillCircle(sx(x, 16), sy(bobY, 16), sr(14), col);
            _canvas->fillCircle(sx(x, 16), sy(bobY, 16), sr(7), TFT_BLACK);
            _canvas->fillCircle(sx(x, 19), sy(bobY, 12), sr(2), TFT_WHITE);
            if (_state == PET_THINKING) {
                int px = sx(x, 16) + sp((int)(sinf(_frame * 0.8f) * 3));
                int py = sy(bobY, 16) + sp((int)(cosf(_frame * 0.8f) * 3));
                _canvas->fillCircle(px, py, sr(5), TFT_BLACK);
            }
            _canvas->fillRect(sx(x, 3), sy(bobY, 3), sp(26), sp(4), TFT_BLACK);
        } else {
            _canvas->drawLine(sx(x, 3), sy(bobY, 16), sx(x, 29), sy(bobY, 16), col);
        }
    }

    // ── CRT Bot ──────────────────────────────
    void drawCRTBot(int x, int y) {
        uint16_t col = getAccentColor();
        int bobY = y + sp((_frame / 4) % 2);

        _canvas->fillRoundRect(sx(x, 3), sy(bobY, 4), sp(27), sp(23), sr(4), col);
        _canvas->fillRect(sx(x, 7), sy(bobY, 7), sp(18), sp(12), TFT_BLACK);

        for (int sl = 0; sl < 6; sl += 2)
            _canvas->drawLine(sx(x, 7), sy(bobY, 7 + sl * 2), sx(x, 24), sy(bobY, 7 + sl * 2), 0x0820);

        _canvas->setTextColor(col);
        _canvas->setTextSize(1);
        if (_state == PET_THINKING)      _canvas->drawString("...",                 sx(x, 10), sy(bobY, 11));
        else if (_state == PET_TALKING)  _canvas->drawString(_frame%2?">>":"OK",    sx(x, 11), sy(bobY, 11));
        else if (_state == PET_ERROR)  { _canvas->setTextColor(TFT_RED);
                                         _canvas->drawString("ERR",                 sx(x, 9), sy(bobY, 11)); }
        else                             _canvas->drawString("_",                   sx(x, 14), sy(bobY, 11));

        _canvas->drawLine(sx(x, 16), sy(bobY, 4), sx(x, 16), sy(bobY, -4), col);
        _canvas->fillCircle(sx(x, 16), sy(bobY, -4), sr(2), col);
        _canvas->fillRoundRect(sx(x, 7), sy(bobY, 24), sp(18), sp(8), sr(2), col);
    }

    // ── Drone ────────────────────────────────
    void drawDrone(int x, int y) {
        uint16_t col = getAccentColor();
        int bobY = y + sp((int)(sinf(_frame * 0.6f) * 2));

        _canvas->fillRoundRect(sx(x, 10), sy(bobY, 10), sp(12), sp(12), sr(2), col);

        int rotorPhase = _frame % 4;
        const int rx[] = {sx(x, 3), sx(x, 3), sx(x, 25), sx(x, 25)};
        const int ry[] = {sy(bobY, 6), sy(bobY, 22), sy(bobY, 6), sy(bobY, 22)};
        for (int r = 0; r < 4; r++) {
            _canvas->fillCircle(rx[r], ry[r], sr(4), TFT_DARKGREY);
            _canvas->drawLine(rx[r], ry[r], rx[r] + sp(rotorPhase < 2 ? 3 : -3), ry[r], col);
        }

        uint16_t ledCol = (_state == PET_ERROR) ? TFT_RED
                        : (_state == PET_SLEEPING) ? TFT_BLACK
                        : TFT_WHITE;
        _canvas->fillCircle(sx(x, 16), sy(bobY, 16), sr(3), ledCol);
    }

    // ── Blob ─────────────────────────────────
    void drawBlob(int x, int y) {
        uint16_t col = getAccentColor();
        int scaleX = sr(14 + (int)(sinf(_frame * 0.4f) * 2));
        int scaleY = sr(12 - (int)(sinf(_frame * 0.4f) * 2));
        int bobY   = y + sp(18) - scaleY;

        _canvas->fillEllipse(sx(x, 16), bobY, scaleX, scaleY, col);

        bool eyesClosed = _blinking || (_state == PET_SLEEPING);
        if (!eyesClosed) {
            _canvas->fillCircle(sx(x, 12), bobY - sr(2), sr(2), TFT_WHITE);
            _canvas->fillCircle(sx(x, 20), bobY - sr(2), sr(2), TFT_WHITE);
            _canvas->fillCircle(sx(x, 13), bobY - sr(2), sr(1), TFT_BLACK);
            _canvas->fillCircle(sx(x, 21), bobY - sr(2), sr(1), TFT_BLACK);
        } else {
            _canvas->drawLine(sx(x, 10), bobY - sr(2), sx(x, 14), bobY - sr(2), TFT_WHITE);
            _canvas->drawLine(sx(x, 18), bobY - sr(2), sx(x, 22), bobY - sr(2), TFT_WHITE);
        }

        if (_state == PET_TALKING && _frame % 2 == 0)
            _canvas->fillEllipse(sx(x, 16), bobY + sp(4), sr(3), sr(2), TFT_WHITE);
        else
            _canvas->drawLine(sx(x, 13), bobY + sp(4), sx(x, 19), bobY + sp(4), TFT_WHITE);
    }
};
