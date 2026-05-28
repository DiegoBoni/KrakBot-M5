# KRAKBOT — Companion OS for M5Stack Cardputer ADV

> Un sistema operativo de compañía para el M5Stack Cardputer ADV. Chat con IA, mascotas animadas, sonidos por mascota, y configuración vía web. Todo en un dispositivo del tamaño de una tarjeta de crédito.

---

## ¿Qué es?

KRAKBOT es un firmware para el **M5Stack Cardputer ADV** que convierte el dispositivo en una terminal de chat con IA. Conectás a tu red WiFi, configurás tu provider de IA (OpenAI, n8n webhook, o Claude Gateway), y tenés un compañero de bolsillo con personalidad propia.

**Features:**
- Chat en tiempo real con OpenAI GPT, n8n workflows, o Claude Gateway
- 5 mascotas animadas con personalidad y sonidos únicos: Kraken, Eye, CRTBot, Drone, Blob
- Boot screen animado con barra de progreso sinusoidal
- UI de chat con scroll, tipografía terminal, mascota lateral animada
- Sonidos de interacción por mascota (envío, respuesta, error)
- Panel web responsive para configurar todo desde el browser o el celu
- Sistema de menús en el dispositivo sin tocar el browser
- Configuración persistente en flash (LittleFS)
- Arquitectura dual-core: UI en Core 1, HTTP en Core 0

---

## Hardware requerido

- **M5Stack Cardputer ADV** (SKU: K132-Adv)
  - ESP32-S3 @ 240MHz
  - Display ST7789V2 1.14" 240×135px
  - Teclado 56 teclas (TCA8418)
  - ES8311 codec + NS4150B speaker
  - 8MB Flash, PSRAM

---

## Instalación rápida (sin compilar)

### Opción A — ESP Web Flasher (recomendado)

1. Conectá el Cardputer ADV en modo download:
   - Apagá el dispositivo (switch lateral a OFF)
   - Mantené presionado `G0`
   - Encendé (switch a ON) y soltá `G0`
2. Entrá a [https://espressif.github.io/esptool-js/](https://espressif.github.io/esptool-js/)
3. Conectá por USB, seleccioná el puerto
4. Flasheá el archivo `firmware/KRAKBOT.merged.bin` en la dirección `0x0`
5. Reiniciá el dispositivo

### Opción B — esptool (línea de comando)

```bash
pip install esptool

esptool.py --chip esp32s3 --port /dev/ttyUSB0 --baud 921600 \
  write_flash 0x0 firmware/KRAKBOT.merged.bin
```

> En macOS el puerto suele ser `/dev/cu.usbmodem*`, en Windows `COM3` o similar.

---

## Primera configuración

Al arrancar por primera vez sin WiFi configurado, KRAKBOT levanta un **Access Point**:

1. Conectate a la red `KRAKBOT-SETUP` desde tu celu o compu
2. Abrí el browser en `192.168.4.1`
3. Configurá WiFi → guardá → el dispositivo se reconecta
4. Configurá el Brain (OpenAI key, n8n webhook URL, o Claude Gateway URL)
5. Listo — empezá a chatear

---

## Panel web

Con WiFi conectado, la IP local aparece en la pantalla del dispositivo. Abrila en cualquier browser de la misma red.

| Sección | Qué configurás |
|---------|----------------|
| **WiFi** | SSID y password de tu red |
| **Brain** | Provider (OpenAI / n8n / Claude Gateway) y credenciales |
| **Audio** | TTS on/off, voz, volumen |
| **Pet** | Mascota activa y nombre |
| **Chat** | Enviá mensajes directo desde el browser |

> El panel web es solo accesible desde tu red local. No hay exposición a internet.

---

## Controles del teclado

### Escritura y chat

| Tecla | Acción |
|-------|--------|
| `Enter` | Enviar mensaje |
| `Del` | Borrar último caracter |
| `Fn + ;` | Scroll chat arriba |
| `Fn + .` | Scroll chat abajo |

### Menús

| Atajo | Menú |
|-------|------|
| `Fn + B` | Brain — cambiar provider de IA |
| `Fn + P` | Pet — cambiar mascota |
| `Fn + M` | Audio — configurar TTS y volumen |
| `Fn + W` | Toggle WebServer (ON/OFF) |
| `Fn + S` | Toggle Sonidos (ON/OFF) |

### Navegación en menús

| Tecla | Acción |
|-------|--------|
| `Fn + ;` | Subir en el menú |
| `Fn + .` | Bajar en el menú |
| `Enter` | Confirmar selección |
| `Del` | Cerrar menú |

---

## Mascotas y sonidos

Cada mascota tiene animación propia, personalidad distinta, y firma sonora única.

| Mascota | Sonido al enviar | Sonido al responder |
|---------|-----------------|---------------------|
| **Kraken** | Bloop descendente | Gargareo de las profundidades |
| **Eye** | Bip agudo | Barrido escáner |
| **CRTBot** | Doble pulso retro | Melodía 8-bit |
| **Drone** | Zumbido grave | Vibrato mecánico |
| **Blob** | Burbuja triple | Burbujas random |

---

## Brain providers

### OpenAI Direct
Conecta directo a la API de OpenAI. Necesitás una API key (`sk-...`). Soporta `gpt-4o-mini`, `gpt-4o`, `gpt-4-turbo`, `gpt-3.5-turbo`.

### n8n Webhook
Mandá los mensajes a un workflow de n8n. El Cardputer hace POST a tu webhook con `{"message": "..."}` y espera `{"response": "..."}`. Perfecto para conectar con herramientas, bases de datos, o cualquier servicio externo.

### Claude Gateway
Conectá a un servidor propio que actúe de intermediario hacia la API de Claude u otros modelos. Útil para tener contexto persistente, herramientas custom, o integrar con otros sistemas como Telegram.

---

## Compilar desde fuente

### Requisitos

- Arduino IDE 2.x
- Board: **M5Stack** (instalar desde Board Manager: `https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/arduino/package_m5stack_index.json`)
- Librerías:
  - `M5Cardputer`
  - `ArduinoJson` >= 7.x
  - `LittleFS` (incluida en el board package)

### Estructura del proyecto

```
KrakBot-M5/
├── KRAKBOT/
│   └── KRAKBOT.ino        # Sketch principal
├── config.h               # Structs de configuración
├── Storage.h              # Persistencia LittleFS
├── WifiManager.h          # WiFi + AP mode
├── BrainManager.h         # Lógica de chat por provider
├── WebConfig.h            # Panel web + API REST
├── Pet.h                  # Mascotas animadas
├── AudioManager.h         # ES8311 + I2S (en desarrollo)
└── firmware/
    └── KRAKBOT.merged.bin # Binario listo para flashear
```

### Compilar

1. Abrí `KRAKBOT/KRAKBOT.ino` en Arduino IDE
2. Seleccioná board: **M5Stack Cardputer** (o M5Stack Cardputer ADV)
3. `Sketch → Export Compiled Binary`
4. Flasheá con el IDE o con esptool

---

## Seguridad

- La IP del panel web es **privada** — solo accesible desde tu red local
- Las credenciales (API keys, tokens) se guardan en el flash del dispositivo
- El WebServer se puede apagar con `Fn + W` cuando no lo necesitás
- Por ahora no tiene autenticación en el panel web — próximamente Basic Auth

---

## Estado del proyecto

| Feature | Estado |
|---------|--------|
| Chat OpenAI | ✅ Funcionando |
| Chat n8n Webhook | ✅ Funcionando |
| Chat Claude Gateway | ✅ Funcionando |
| UI animada + mascotas | ✅ Funcionando |
| Panel web responsive | ✅ Funcionando |
| Sonidos por mascota | ✅ Funcionando |
| Menús en dispositivo | ✅ Funcionando |
| TTS / STT (Whisper) | 🔧 En desarrollo (conflicto I2S con M5) |
| Historial por mascota | ✅ Funcionando |
| Basic Auth web panel | 📋 Próximamente |

---

## Créditos

Desarrollado por [@DiegoBoni](https://github.com/DiegoBoni)

Parte del ecosistema [KrakBot](https://krakbot.app) — companion AI para humanos raros.
