🤖 EMOtto 3.0 — ESP32 Emotional Robot Platform








EMOtto 3.0 is an open-source emotional robot platform built around ESP32-S3 boards.
It combines animated facial expressions, audio playback, servo motion, BLE communication, sensors and AI camera vision into a modular robotics framework for makers and education.

🖼 Hardware Overview
Display & Controller	ESP32-S3 Camera	Main Components

	
	


	
	
🤖 EMOtto Assembled

✨ Main Features
🎭 Animated Facial Expressions

Eyes and mouth rendered on TFT display

Expression engine with emotional states

Smooth transitions and blinking

RGB565 .bin facial assets

🔊 Audio & Voice

DFPlayer Mini integration

Sounds synchronized with emotions

Voice lines and reactions

🦿 Motion System

PCA9685 16-channel servo driver

Designed for Otto-style walking robots

Expression-linked movements

🌈 Aura LED Effects

WS2812 LED ring

Emotion-based color animations

📡 Bluetooth Low Energy

Powered by NimBLE-Arduino

Mobile app control

Expression, movement and sound commands

📷 AI Camera Vision

Handled by a second ESP32 using EloquentEsp32cam:

Face detection

Face recognition

WiFi video streaming

🧠 System Architecture
Board	Role
ESP32-S3 LCD (Waveshare)	Display, expressions, servos, audio, BLE
ESP32-S3 CAM	Camera streaming and face recognition

The boards communicate via UART.

📚 Required Arduino Libraries

See Libraries_Info/required_libraries.md for the complete list.

Main libraries include:

NimBLE-Arduino

EloquentEsp32cam

LVGL

Adafruit PWM Servo Driver

DFRobotDFPlayerMini

Adafruit VL53L0X

FastLED / NeoPixel

⚙️ ESP32-S3 CAM Configuration
Setting	Value
Board	ESP32S3 Dev Module
Flash Size	16MB
Partition Scheme	8M with SPIFFS
PSRAM	OPI PSRAM (Required)
📁 Repository Structure
Firmware_LCD/      → Main robot firmware
Firmware_CAM/      → Camera + recognition firmware
Assets/            → Faces and audio assets
Docs/              → Images and hardware documentation
Libraries_Info/    → Required libraries list

📜 License

Released under GNU GPL v3.0 or later.

👨‍💻 Author

Marco Mortari
📧 marco.mortari73@gmail.com

🔗 https://github.com/MortariMarco

❤️ Contributing

Ideas, improvements and pull requests are welcome.
EMOtto is an open platform built for the maker community.
