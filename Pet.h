#pragma once
#include <M5Cardputer.h>
#include "config.h"

class Pet {
public:
    void setCanvas(LGFX_Sprite* canvas) { _canvas = canvas; }

    void setType(PetType type) {
        _type = type;
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

        // Movimiento lateral: sway lento en idle, más amplio en talking/listening
        int sway = 0;
        int bob  = 0;
        switch (_state) {
            case PET_IDLE:
                // Paseo suave de lado a lado: ciclo largo, amplitud ±6px
                sway = (int)(sinf(_frame * 0.18f) * 6.0f);
                bob  = (int)(sinf(_frame * 0.36f) * 1.5f);
                break;
            case PET_LISTENING:
                // Más inquieto, se inclina hacia donde escucha
                sway = (int)(sinf(_frame * 0.45f) * 4.0f);
                bob  = (int)(cosf(_frame * 0.45f) * 2.0f);
                break;
            case PET_TALKING:
                // Bounce animado mientras habla
                sway = (int)(sinf(_frame * 0.6f) * 3.0f);
                bob  = (_frame % 2 == 0) ? -1 : 1;
                break;
            case PET_THINKING:
                // Giro lento, casi estático
                sway = (int)(sinf(_frame * 0.1f) * 2.0f);
                break;
            default:
                break;
        }

        switch (_type) {
            case PET_KRAKEN: drawKraken(x + sway, y + bob); break;
            case PET_EYE:    drawEye(x + sway, y + bob);    break;
            case PET_CRTBOT: drawCRTBot(x + sway, y + bob); break;
            case PET_DRONE:  drawDrone(x + sway, y + bob);  break;
            case PET_BLOB:   drawBlob(x + sway, y + bob);   break;
        }
    }

    uint16_t getAccentColor() const {
        // Kraken: rojo coral emoji 🐙 #E05050 → 0xE28A
        // Eye: magenta | CRTBot: verde turquesa | Drone: naranja | Blob: beige
        const uint16_t colors[] = {0xE28A, 0xF81F, 0x07F9, 0xFD20, 0xD69A};
        return colors[(int)_type];
    }

private:
    LGFX_Sprite* _canvas = nullptr;
    PetType _type = PET_KRAKEN;
    PetState _state = PET_IDLE;
    int _frame = 0;
    uint32_t _lastFrame = 0;
    int _blinkTimer = 0;
    bool _blinking = false;
    static constexpr int PIX = 5;

    uint32_t frameInterval() const {
        switch (_state) {
            case PET_THINKING:  return 180;
            case PET_TALKING:   return 130;
            case PET_LISTENING: return 240;
            case PET_SLEEPING:  return 900;
            case PET_ERROR:     return 120;
            default:            return 500;
        }
    }

    void px(int x, int y, int gx, int gy, uint16_t c) {
        _canvas->fillRect(x + gx * PIX, y + gy * PIX, PIX, PIX, c);
    }

    void block(int x, int y, int gx, int gy, int gw, int gh, uint16_t c) {
        _canvas->fillRect(x + gx * PIX, y + gy * PIX, gw * PIX, gh * PIX, c);
    }

    void eyes(int x, int y, int lx, int ly, int rx, int ry) {
        if (_blinking || _state == PET_SLEEPING) {
            block(x, y, lx, ly, 2, 1, TFT_WHITE);
            block(x, y, rx, ry, 2, 1, TFT_WHITE);
        } else {
            block(x, y, lx, ly, 2, 2, TFT_WHITE);
            block(x, y, rx, ry, 2, 2, TFT_WHITE);
            px(x, y, lx + 1, ly + ((_frame / 2) % 2), TFT_BLACK);
            px(x, y, rx + 1, ry + ((_frame / 2) % 2), TFT_BLACK);
        }
    }

    void talkMouth(int x, int y, int gx, int gy) {
        if (_state == PET_TALKING && (_frame % 2 == 0)) {
            block(x, y, gx, gy, 2, 2, TFT_WHITE);
        } else {
            block(x, y, gx, gy, 2, 1, TFT_WHITE);
        }
    }

    void thinkingDots(int x, int y, int gx, int gy) {
        if (_state != PET_THINKING) return;
        px(x, y, gx + (_frame % 3) * 2, gy, TFT_YELLOW);
        px(x, y, gx + ((_frame + 1) % 3) * 2, gy + 1, TFT_YELLOW);
    }

    void errorMark(int x, int y, int gx, int gy) {
        if (_state != PET_ERROR) return;
        px(x, y, gx, gy, TFT_RED);
        px(x, y, gx, gy + 1, TFT_RED);
        px(x, y, gx, gy + 3, TFT_RED);
    }

    void drawKraken(int x, int y) {
        uint16_t col = getAccentColor();
        int bob = (_frame % 4 == 0) ? 1 : 0;
        y += bob;

        block(x, y, 3, 2, 6, 4, col);
        block(x, y, 2, 3, 8, 2, col);
        block(x, y, 4, 1, 4, 1, col);
        eyes(x, y, 4, 3, 7, 3);
        talkMouth(x, y, 5, 5);

        int tent = (_frame % 2 == 0) ? 1 : 0;
        px(x, y, 2, 6, col);
        px(x, y, 3, 7 - tent, col);
        px(x, y, 4, 6 + tent, col);
        px(x, y, 5, 7, col);
        px(x, y, 6, 6 + tent, col);
        px(x, y, 7, 7 - tent, col);
        px(x, y, 8, 6, col);
        px(x, y, 9, 7, col);

        thinkingDots(x, y, 4, 0);
        errorMark(x, y, 9, 1);
    }

    void drawEye(int x, int y) {
        uint16_t col = getAccentColor();
        block(x, y, 2, 2, 8, 5, col);
        block(x, y, 3, 1, 6, 1, col);
        block(x, y, 3, 7, 6, 1, col);
        block(x, y, 4, 3, 4, 3, TFT_WHITE);

        if (_blinking || _state == PET_SLEEPING) {
            block(x, y, 4, 4, 4, 1, TFT_BLACK);
        } else {
            int look = (_state == PET_THINKING) ? ((_frame % 3) - 1) : 0;
            block(x, y, 5 + look, 3, 2, 3, TFT_BLACK);
            px(x, y, 7 + look, 3, TFT_WHITE);
        }

        if (_state == PET_LISTENING) {
            px(x, y, 1, 3, TFT_YELLOW);
            px(x, y, 10, 3, TFT_YELLOW);
        }
        errorMark(x, y, 10, 1);
    }

    void drawCRTBot(int x, int y) {
        uint16_t col = getAccentColor();
        block(x, y, 2, 1, 8, 6, col);
        block(x, y, 3, 2, 6, 4, TFT_BLACK);
        px(x, y, 5, 0, col);
        px(x, y, 6, 0, col);
        block(x, y, 4, 7, 1, 2, col);
        block(x, y, 7, 7, 1, 2, col);

        for (int i = 0; i < 3; ++i) {
            block(x, y, 3, 2 + i, 6, 1, (i % 2 == 0) ? 0x0841 : TFT_BLACK);
        }

        if (_state == PET_THINKING) {
            block(x, y, 4, 4, 1, 1, TFT_WHITE);
            block(x, y, 6, 4, 1, 1, TFT_WHITE);
            block(x, y, 8, 4, 1, 1, TFT_WHITE);
        } else if (_state == PET_TALKING) {
            block(x, y, 4, 4, 4, 1, TFT_WHITE);
            px(x, y, 8, 3 + (_frame % 2), TFT_WHITE);
        } else {
            eyes(x, y, 4, 3, 7, 3);
            talkMouth(x, y, 5, 5);
        }

        errorMark(x, y, 9, 1);
    }

    void drawDrone(int x, int y) {
        uint16_t col = getAccentColor();
        int spin = (_frame % 2 == 0) ? 0 : 1;
        block(x, y, 4, 3, 4, 3, col);
        block(x, y, 2, 1, 2, 1, col);
        block(x, y, 8, 1, 2, 1, col);
        block(x, y, 2, 7, 2, 1, col);
        block(x, y, 8, 7, 2, 1, col);
        block(x, y, 3, 2, 1, 1, col);
        block(x, y, 8, 2, 1, 1, col);
        block(x, y, 3, 6, 1, 1, col);
        block(x, y, 8, 6, 1, 1, col);

        block(x, y, 1, spin ? 1 : 0, 3, 1, TFT_DARKGREY);
        block(x, y, 8, spin ? 0 : 1, 3, 1, TFT_DARKGREY);
        block(x, y, 1, spin ? 7 : 8, 3, 1, TFT_DARKGREY);
        block(x, y, 8, spin ? 8 : 7, 3, 1, TFT_DARKGREY);

        uint16_t led = (_state == PET_ERROR) ? TFT_RED
                     : (_state == PET_LISTENING) ? TFT_YELLOW
                     : TFT_WHITE;
        block(x, y, 5, 4, 2, 1, led);
        talkMouth(x, y, 5, 5);
    }

    void drawBlob(int x, int y) {
        uint16_t col = getAccentColor();
        int squish = (_frame % 3 == 0) ? 1 : 0;
        block(x, y, 3, 2 + squish, 6, 5 - squish, col);
        block(x, y, 2, 4, 8, 2, col);
        block(x, y, 4, 1 + squish, 4, 1, col);
        eyes(x, y, 4, 3 + squish, 7, 3 + squish);
        talkMouth(x, y, 5, 5 + squish);
        if (_state == PET_LISTENING) {
            px(x, y, 2, 2, TFT_YELLOW);
            px(x, y, 9, 2, TFT_YELLOW);
        }
        thinkingDots(x, y, 4, 0);
    }
};
