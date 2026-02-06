# 🤖 EMOtto 3.0 — ESP32 Emotional Robot Platform

<p align="center">
  <img src="Docs/images/Emotto%203.0.png" alt="EMOtto 3.0" width="400">
</p>

![MCU](https://img.shields.io/badge/MCU-ESP32--S3-green.svg)  
![Framework](https://img.shields.io/badge/framework-Arduino-orange.svg)  
![Connectivity](https://img.shields.io/badge/Connectivity-BLE%20%7C%20WiFi-blue.svg)  
![Status](https://img.shields.io/badge/status-Active%20Development-brightgreen)
![License](https://img.shields.io/badge/license-GPL--3.0-blue.svg)
![Repo Size](https://img.shields.io/github/repo-size/MortariMarco/Emotto3.0_CamPCA9685)
![Last Commit](https://img.shields.io/github/last-commit/MortariMarco/Emotto3.0_CamPCA9685)

**EMOtto 3.0** is an open-source emotional robot platform based on **two ESP32-S3 boards**, designed to combine **animated expressions, motion, sound, and AI vision** into a single interactive companion.

A creative robotics platform for **makers, educators, and developers**.

---

## 🚀 Getting Started

1. Flash the **CAM board** → `Firmware_CAM/`  
2. Flash the **LCD board** → `Firmware_LCD/`  
3. Follow wiring guide → **Docs/hardware_notes.md**  
4. Configure Arduino IDE → **firmware_setup.md**  
5. Install required libraries → **required_libraries.md**

---

## 🧠 System Architecture

EMOtto uses **two independent ESP32-S3 boards working together**.

- **WiFi** is used for camera streaming  
- **UART (Serial1)** is used for command/control between CAM ↔ LCD  

| Board | Role |
|------|------|
| 🖥 **Waveshare ESP32-S3 LCD board** | Face animations, audio playback, servo control, sensors, BLE |
| 📷 **ESP32-S3 N16R8 CAM** | Camera streaming and face recognition (Eloquent) |

---

## 🔗 Quick Links

| Section | Link |
|--------|------|
| LCD Firmware | [Firmware_LCD](Firmware_LCD/) |
| CAM Firmware | [Firmware_CAM](Firmware_CAM/) |
| Hardware Notes & Wiring | [Docs/hardware_notes.md](Docs/hardware_notes.md) |
| Required Libraries | [required_libraries.md](required_libraries.md) |
| Firmware Setup (Arduino IDE) | [firmware_setup.md](firmware_setup.md) |
| Components (BOM) | [components.md](components.md) |
| Estimated Cost | [cost_estimate.md](cost_estimate.md) |
| Face Assets (.bin) | [Assets/facce_bin](Assets/facce_bin/) |
| Audio Files | [Assets/mp3](Assets/mp3/) |
| All Images | [Docs/images](Docs/images/) |

---

## 📱 Mobile App Interface

<p align="center">
  <img src="Docs/images/App.png" width="300">
  <img src="Docs/images/App2.png" width="300">
</p>

The Android companion app allows:

- Expression selection  
- Motion control  
- Volume adjustment  
- Camera live stream  

---

## 🧩 Hardware Overview

| Component | Function |
|----------|----------|
| ESP32-S3 LCD Board | Main controller & display |
| ESP32-S3 CAM | Vision processor |
| PCA9685 | 16-channel servo driver |
| DFPlayer Mini | Audio playback |
| VL53L0X | Distance / interaction sensing |
| WS2812 LED Ring | Emotional “aura” lighting |

---

## 🤖 Real Build Photos

<p align="center">
  <img src="Docs/images/20260122_130100.jpg" width="260">
  <img src="Docs/images/20260122_130111.jpg" width="260"><br>
  <img src="Docs/images/20260122_130144.jpg" width="260">
  <img src="Docs/images/20260122_141649.jpg" width="260">
</p>

---

## ✨ Main Features

- 🎭 Animated facial expressions (eyes + mouth)
- 🔊 Sound & voice via DFPlayer Mini
- 🦿 Multi-servo motion using PCA9685
- 🌈 WS2812 RGB LED aura effects
- 📡 BLE control using NimBLE
- 📷 AI camera vision using EloquentEsp32cam
- 🧭 Motion awareness via QMI8658 IMU (fall detection & recovery)
- 👤 **Face recognition with personalized greetings**

---

---

## 👤 Face Recognition & Personalized Interaction

EMOtto can **recognize people and greet them by name**.

Using the camera module and on-device AI vision, the robot can identify previously enrolled faces and trigger personalized behaviors.

### 📱 How It Works

Through the mobile app, users can activate **Enroll Mode**:

1. The camera detects a **new, unknown face**
2. The app asks if you want to save the person
3. You enter a **name**
4. The face is stored in the recognition database

From that moment on, when the person appears again, EMOtto can:

- Recognize them  
- Trigger a dedicated greeting expression  
- Play a personalized audio response  

This feature allows EMOtto to move from a generic robot to a **socially aware companion**.

---

## 🧠 Technical Overview — Face Enrollment & Recognition

EMOtto uses on-device vision processing to perform **face detection and recognition** directly on the ESP32-S3 CAM board.

### 📸 Face Detection & Recognition

The camera module runs the **EloquentEsp32cam** library to:

- Detect faces in the camera frame
- Extract facial features
- Compare them with stored face profiles

All processing happens **locally on the robot**, without needing cloud services.

---

### 📂 Face Database Storage

When a new person is enrolled through the app:

1. The system captures multiple face samples
2. A facial profile is generated
3. The profile is stored in the ESP32-S3 CAM memory (SPIFFS / Flash)

Each stored profile is linked to:

- A **person’s name**
- A unique **face ID**

This allows EMOtto to associate a recognized face with a specific identity.

---

### 👋 Recognition Workflow

When the camera detects a face:

1. The system checks if the face matches a stored profile  
2. If a match is found:
   - The associated **name** is returned  
   - A personalized greeting can be triggered  
3. If the face is **unknown**:
   - The app can prompt the user to enroll the new person

---

### ⚙️ System Limits

Due to memory and processing constraints of the ESP32-S3:

- The number of stored faces is **limited**
- Recognition works best with:
  - Good lighting  
  - Frontal face view  
  - Moderate distance from camera  

Despite these limits, the system provides a **lightweight embedded face recognition solution** suitable for interactive robotics.

---

### 🔒 Privacy by Design

All facial data is stored **locally on the device**.  
EMOtto does **not** upload images or biometric data to external servers.

This makes the system suitable for educational and home environments where privacy is important.

---


## 🚧 Project Status & Roadmap

EMOtto 3.0 is currently in **active development**.  
New behaviors and capabilities are continuously being added.

### 🧠 Emotional Engine (in progress)
The robot already supports multiple animated expressions and synchronized behaviors.  
Upcoming additions include:

- More complex emotional states  
- Context-aware reactions  
- Expanded animation sets  

### 🤖 Autonomous Behaviors (planned & in development)

EMOtto is evolving from an expressive robot into an **interactive autonomous companion**.

Planned features include:

- 🏃 Face / person following behavior  
- 🚧 Obstacle avoidance using distance sensing  
- 👋 Social interaction routines  

### 🧭 Motion Awareness (already implemented)

Using the **QMI8658 IMU sensor**, EMOtto can:

- Detect when it has fallen  
- Understand its orientation  
- Trigger recovery animations when standing up  

This enables more lifelike and safe motion behavior.

---


## ⚙️ ESP32-S3 CAM Settings (Arduino IDE)

| Setting | Value |
|--------|------|
| Board | ESP32S3 Dev Module |
| Flash Size | 16MB |
| Partition Scheme | 8M with SPIFFS |
| PSRAM | **OPI PSRAM (REQUIRED)** |

---

## 🗂 Repository Structure

- `Firmware_LCD/` — main robot firmware (display, BLE, servos, audio)
- `Firmware_CAM/` — camera + recognition + streaming server
- `libraries/EMOtto32_pca9685/` — project core library (included)
- `Assets/` — face `.bin` files and audio `.mp3`
- `Docs/` — photos and wiring documentation

---

---

## 🔮 Future Vision

EMOtto 3.0 is more than a robot — it is a platform for exploring the intersection of **emotion, interaction, and autonomous behavior**.

The long-term vision is to evolve EMOtto into:

### 🤖 An Expressive Companion
A robot capable of showing emotions through synchronized **face animations, movement, sound, and lighting**, creating a believable and engaging presence.

### 🧠 An Interactive Learning Platform
EMOtto is designed as a **maker-friendly educational platform**, helping students and developers explore:

- Robotics
- Embedded systems
- Human-robot interaction
- AI-based perception

### 🌍 A Modular Open Robotics System
The project is fully open-source and built to be **expandable**:

- Add new sensors  
- Create new emotions  
- Implement autonomous behaviors  
- Customize hardware and appearance  

EMOtto aims to be a bridge between **creative robotics** and **technical learning**, where personality and engineering meet.

---

## 📜 License

This project is released under the **GPL-3.0-only License**.

---

## 👨‍💻 Author

**Marco Mortari**  
📧 marco.mortari73@gmail.com  
🔗 https://github.com/MortariMarco  

---

## ❤️ Contributing

Ideas, improvements, and pull requests are welcome.  
EMOtto is an open platform built for the maker community.




