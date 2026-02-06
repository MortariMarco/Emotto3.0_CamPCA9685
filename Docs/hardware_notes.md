🔌 EMOtto 3.0 — Hardware Notes & Wiring

This document describes the hardware configuration and wiring used in EMOtto 3.0.

🧠 System Overview

EMOtto 3.0 uses two ESP32-S3 boards:

Board	Function
ESP32-S3 LCD (Waveshare)	Robot brain: display, expressions, servos, audio, BLE
ESP32-S3 N16R8 CAM	Camera streaming and face recognition

The two boards communicate via UART (Serial1).

📷 ESP32-S3 CAM Settings (Arduino IDE)
Setting	Value
Board	ESP32S3 Dev Module
Flash Size	16MB
Partition Scheme	8M with SPIFFS
PSRAM	OPI PSRAM (REQUIRED)

Without PSRAM enabled, face recognition will not work.

🖥 LVGL Display Configuration

To disable debug widgets on screen, edit lv_conf.h:

#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR  0

🔌 UART Communication (CAM ↔ LCD Board)
ESP32-S3 CAM	ESP32-S3 LCD
GPIO2 (TX) →	RX
GPIO1 (RX) ←	TX
GND ↔	GND

Power both boards from the same 5V supply and share GND.

🦿 PCA9685 Servo Driver
PCA9685 Pin	ESP32-S3 Pin
SDA	GPIO11
SCL	GPIO10
VCC	3.3V
V+	5V (servo power only)

Use an external 5V supply for servos.

📏 VL53L0X ToF Distance Sensor
Sensor Pin	ESP32 Pin
SDA	GPIO11
SCL	GPIO10
VIN	5V
GND	GND
🔊 DFPlayer Mini Audio Module
DFPlayer Pin	ESP32 Pin
RX	GPIO18
TX	GPIO17
VCC	5V
GND	GND
🌈 WS2812 LED Ring
Signal	Connection
DIN	U0TXD (through 330Ω resistor)
VCC	5V
GND	GND

optional: Add a 470–1000µF capacitor across 5V and GND near the LEDs.

⚡ Power Supply Notes

Use a 5V UBEC rated at ≥ 2A (3A recommended).

Use star wiring: separate 5V lines to LCD board, CAM board, PCA9685 (servo power).

Always connect all grounds together.

⚠️ Known Hardware Notes

Ensure PSRAM is enabled on the CAM board

Add capacitors near the CAM to avoid WiFi resets

Use a common ground to avoid communication issues
