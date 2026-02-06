# 🔌 EMOtto 3.0 — Hardware Notes & Wiring

This document describes the **hardware configuration and wiring** used in **EMOtto 3.0**.

---

## 🧠 System Overview

EMOtto 3.0 uses **two ESP32-S3 boards**:

| Board | Function |
|------|----------|
| **ESP32-S3 LCD (Waveshare)** | Robot brain: display, expressions, servos, audio, BLE |
| **ESP32-S3 N16R8 CAM** | Camera streaming and face recognition |

The two boards communicate via **UART (Serial1)**.

---

## 📷 ESP32-S3 CAM Settings (Arduino IDE)

| Setting | Value |
|--------|------|
| Board | ESP32S3 Dev Module |
| Flash Size | 16MB |
| Partition Scheme | 8M with SPIFFS |
| PSRAM | **OPI PSRAM (REQUIRED)** |

⚠️ Without PSRAM enabled, **face recognition will not work**.

---

## 🖥 LVGL Display Configuration

To disable debug widgets on screen, edit `lv_conf.h`:

```cpp
#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR  0
```

## 🔌 UART Communication (CAM ↔ LCD Board)

| ESP32-S3 CAM | ESP32-S3 LCD |
|--------------|--------------|
| GPIO2 (TX) → | RX |
| GPIO1 (RX) ← | TX |
| GND ↔ | GND |

⚡ Power both boards from the same **5V supply** and always **share GND**.

---

## 🦿 PCA9685 Servo Driver

| PCA9685 Pin | ESP32-S3 Pin |
|-------------|--------------|
| SDA | GPIO11 |
| SCL | GPIO10 |
| VCC | 3.3V |
| V+  | 5V (servo power only) |

🔋 Use an **external 5V supply** for servos — **do NOT power servos from the ESP32**.

---

## 📏 VL53L0X ToF Distance Sensor

| Sensor Pin | ESP32 Pin |
|------------|-----------|
| SDA | GPIO11 |
| SCL | GPIO10 |
| VIN | 5V |
| GND | GND |

Shares the same **I2C bus** as the PCA9685.

---

## 🔊 DFPlayer Mini Audio Module

| DFPlayer Pin | ESP32 Pin |
|--------------|-----------|
| RX | GPIO18 |
| TX | GPIO17 |
| VCC | 5V |
| GND | GND |

🔈 Use a small speaker (**3W recommended**) connected to **SPK_1 / SPK_2**.

---

## 🌈 WS2812 LED Ring

| Signal | Connection |
|--------|------------|
| DIN | U0TXD (through **330Ω resistor**) |
| VCC | 5V |
| GND | GND |

💡 **Optional but recommended:**
- Add a **470–1000µF capacitor** across 5V and GND near the LEDs  
- Keep the data wire short to reduce noise

---

## ⚡ Power Supply Notes

- Use a **5V UBEC rated ≥ 2A** (3A recommended)
- Use **star wiring**:
  - Separate 5V lines to LCD board
  - CAM board
  - PCA9685 (servo power)
- Always connect **all grounds together**

---

## ⚠️ Known Hardware Notes

- ✅ Ensure **PSRAM is enabled** on the CAM board
- ⚡ Add **capacitors near the CAM** to prevent WiFi brownouts
- 🔗 Use a **common ground** to avoid UART/I2C communication issues
- 🔋 Servos can introduce noise — keep their power line separated from logic 5V

---

## 🧩 I2C Bus Summary

| Device | Address | Bus |
|--------|---------|-----|
| PCA9685 | 0x40 (default) | I2C |
| VL53L0X | 0x29 (default) | I2C |

Both share **SDA/SCL on GPIO11 / GPIO10**.

---

## 🧯 Safety Tips

- Never connect servos directly to the ESP32 5V pin  
- Double-check polarity before powering  
- Disconnect servos while uploading firmware to avoid brownouts

