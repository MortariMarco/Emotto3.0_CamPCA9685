# 📚 EMOtto 3.0 — Required Arduino Libraries

This project depends on the following libraries.

Install them using the **Arduino IDE Library Manager** or from the provided GitHub links.

---

## 📡 Communication

### NimBLE-Arduino  ver 2.3.6
https://github.com/h2zero/NimBLE-Arduino
Used for **Bluetooth Low Energy** communication between EMOtto and mobile apps.

---

## 📷 Camera & AI Vision

### EloquentEsp32cam  ver 2.7.15
Provides **face detection and recognition** features on the ESP32-S3 CAM board.  
https://github.com/eloquentarduino/EloquentEsp32cam

### JPEGDecoder (by Bodmer)  ver 2.0
https://github.com/Bodmer/JPEGDecoder
Used to **decode JPEG images** from the camera stream.

---

## 🖥 Display & Graphics

### LVGL  
Graphics library used to render EMOtto facial expressions on the TFT display.
Install: Demo ESP32-S3-LCD-1.69 from https://www.waveshare.com/wiki/ESP32-S3-LCD-1.69 official Page.
# 📚 EMOtto 3.0 — Additional Library Requirements

| Library Name | Description | Version | Installation Notes |
|--------------|-------------|---------|--------------------|
| **GFX_Library_for_Arduino** | GFX graphical library for ST7789 display | v1.4.9 | Install Online or Install Offline |
| **lvgl** | LVGL graphical library | v8.4.0 | *Install Online* requires copying the **demos** folder into `src` after installation. **Install Offline recommended** |
| **Mylibrary** | Development board pin macro definitions | — | Install Offline |
| **SensorLib** | PCF85063, QMI8658 sensor driver library | v0.2.1 | Install Online or Install Offline |
| **lv_conf.h** | LVGL configuration file | — | Install Offline — must be placed at the **same level as the LVGL library folder** |

---

## 🖥 Arduino IDE Display Widget Fix

To remove the **performance/debug widgets** shown on the display, edit `lv_conf.h` (inside the LVGL library folder) and set:

```cpp
#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR  0
```

---

## 🦿 Motion Control

### Adafruit PWM Servo Driver Library  ver 3.0.2
https://github.com/adafruit/Adafruit-PWM-Servo-Driver-Library
Controls the **PCA9685 16-channel servo driver**.

---

## 🔊 Audio

### DFRobotDFPlayerMini  ver 1.0.6
https://github.com/DFRobot/DFRobotDFPlayerMini
Used to control the **DFPlayer Mini MP3 module**.

---

## 📏 Sensors

### Adafruit VL53L0X  
Library for the **VL53L0X Time-of-Flight distance sensor**.

---

## 🌈 LEDs

### FastLED *or* Adafruit NeoPixel  
Used to control the **WS2812 LED ring** for EMOtto aura effects.

---

## 🧠 ESP32 Board Support

Install **ESP32 boards by Espressif** via the Arduino Boards Manager.  
**Recommended version:** `2.0.17`

