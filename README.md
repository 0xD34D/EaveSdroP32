# EaveSdroP32

## Overview

**EaveSdroP32** is an ESP-IDF based project for ESP32 devices (ESP32-C3, ESP32-S3, etc.) that streams mono audio from an INMP441 I2S microphone over Wi-Fi. The device runs as a Wi-Fi Access Point with a captive portal, serving a web UI and a WebSocket endpoint. Audio is captured from the INMP441 (left channel, mono), sent via WebSocket, and played in real time in the browser using WebAudio. The project is ideal for remote audio monitoring or prototyping wireless audio streaming.

---

## Features

- ESP32 (C3, S3, etc.) as Wi-Fi AP with captive portal DNS
- HTTPS server with static web UI (SPIFFS)
- WebSocket endpoint for real-time mono audio streaming
- I2S audio input from INMP441 (mono, left channel)
- Configurable settings via `idf.py menuconfig`
- Simple wiring and deployment (Keep It Simple, Stupid)

---

## Getting Started

### 1. Clone the Repository

```sh
git clone https://github.com/0xD34D/EaveSdroP32.git
cd EaveSdroP32
```

### 2. Set the Build Target

Set the target to match your ESP32 device (examples below):

- ex: ESP32-C3:
  ```sh
  idf.py set-target esp32c3
  ```
- ex: ESP32-S3:
  ```sh
  idf.py set-target esp32s3
  ```

### 3. Configure Project Settings

Run menuconfig to adjust Wi-Fi and audio settings:

```sh
idf.py menuconfig
```

Key options (see `main/Kconfig`):

- **WiFi AP Settings**
  - `WiFi AP SSID` (default: `EaveSdroP32`)
  - `WiFi AP Password` (default: `3avesdrop`)
  - `Append device MAC to SSID` (optional for unique SSIDs)
- **Audio Stream Settings**
  - `I2S Sample Rate (Hz)` (default: `16000`)
  - `I2S BCLK GPIO` (default: `3`)
  - `I2S WS GPIO` (default: `4`)
  - `I2S Data In GPIO` (default: `2`)

### 4. Build and Flash

Build the project and flash to your ESP32 device (replace `/dev/ttyUSB0` with your serial port):

```sh
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

---

## Hardware Wiring

**INMP441 (I2S Microphone) → ESP32 (C3, S3, etc.)**

| INMP441 Pin | ESP32 GPIO (default) | Description         |
| ----------- | -------------------- | ------------------- |
| VCC         | 3.3V                 | Power               |
| GND         | GND                  | Ground              |
| SD          | GPIO2                | I2S Data In         |
| SCK         | GPIO3                | I2S BCLK            |
| WS          | GPIO4                | I2S Word Select     |
| L/R         | GND                  | Select Left Channel |

- **Note:** Tie INMP441 L/R pin to GND for mono left channel.
- Use only 3.3V for VCC.
- You may change GPIO assignments in `idf.py menuconfig` to match your board.

---

## Usage

1. After flashing, the ESP32 device creates a Wi-Fi AP (SSID as configured).
2. Connect your phone or laptop to the AP.
3. You will be redirected to the web UI (captive portal).
4. Click "Start Stream" to listen to live audio from the INMP441.

---

## Troubleshooting

- No audio? Check wiring and ensure L/R is tied to GND.
- Can't connect? Confirm AP SSID/password in menuconfig.

---

## License

[GPLv3 License](LICENSE.md)