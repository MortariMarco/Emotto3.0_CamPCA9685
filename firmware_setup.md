# ⚙️ EMOtto 3.0 — Firmware Setup Guide

This guide explains how to configure the **Arduino IDE** to upload firmware to both EMOtto boards.

EMOtto 3.0 uses **two ESP32-S3 boards**:

- 🖥 **ESP32-S3 LCD (Waveshare)** → Main robot controller  
- 📷 **ESP32-S3 N16R8 CAM** → Camera streaming + face recognition  

---

## 🧠 Install ESP32 Board Support

1. Open **Arduino IDE**
2. Go to **File → Preferences**
3. Add this URL to *Additional Board Manager URLs*:
https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json

4. Open **Boards Manager**
5. Search for **ESP32 by Espressif Systems**
6. Install version **3.3.0 (recommended)**

---

## 🖥 Firmware Settings — LCD Board (Main Robot)

| Setting | Value |
|--------|------|
| Board | 	Waveshare  ESP32-S3- LCD-1.69 |
| Partition Scheme | Huge apps(3MB no Ota) |
| PSRAM | Enabled  |
| USB Mode | Hardware CDC On Boot |
| Upload Speed | 921600 |

### Notes

- This board runs **display, expressions, audio, servos, BLE**
- Make sure all servos are **disconnected** if you experience brownouts during upload

---

## 📷 Firmware Settings — CAM Board
1. Open **Boards Manager**
2. Search for **ESP32 by Espressif Systems**
3. Install version **2.0.17 (recommended)**

| Setting | Value |
|--------|------|
| Board | ESP32S3 Dev Module |
| Flash Size | 16MB(128MB)   |
| Partition Scheme | 8M with SPIFFS |
| PSRAM | **OPI PSRAM (REQUIRED)** |
| Upload Speed | 921600 |

⚠️ **PSRAM must be enabled** or face recognition will fail.

---

## 🔌 Serial Port Tips

- The LCD board usually appears as **USB CDC**
- The CAM board may require:
  - Press **BOOT** while connecting USB
  - Or hold **BOOT** during upload

If upload fails:
- Try lower speed (460800)
- Use a **short USB cable**
- Disconnect servo power

---

## 🧠 Partition Schemes

| Board | Recommended Partition |
|------|-----------------------|
| LCD | Huge apps(3MB no Ota) |
| CAM | 8M with SPIFFS |

Do **not** use minimal SPIFFS on the CAM board — it may break face model storage.

---

## 🧩 Required Libraries

Make sure you installed all required libraries listed in:

📚 `required_libraries.md`

---

## 🚀 Upload Order

Recommended flashing order when setting up from scratch:

1️⃣ Flash **CAM board firmware**  
2️⃣ Flash **LCD board firmware**  
3️⃣ Power both boards and verify UART communication  

---

## 🧯 Troubleshooting

| Problem | Possible Cause |
|--------|----------------|
| Camera not detected | PSRAM disabled |
| Random resets | Servo power noise |
| BLE not visible | Wrong firmware on LCD board |
| Upload fails | Bad cable or too high upload speed |

---

EMOtto 3.0 is under active development — firmware settings may evolve.
Always check commit notes if something stops working after updates.
