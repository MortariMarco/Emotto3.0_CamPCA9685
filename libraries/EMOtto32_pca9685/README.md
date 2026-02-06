# EMOtto32_pca9685 — EMOtto 3.0 Core Library (ESP32-S3)

This Arduino library is the **core runtime** for **EMOtto 3.0**, an expressive robot platform based on **ESP32-S3**.

It bundles:
- 🎭 Face rendering & animations (LVGL + TFT)
- 📡 BLE commands (NimBLE)
- 🦿 Servo motion (PCA9685)
- 📷 Camera bridge helpers (WiFi CAM + UART triggers)
- 🔊 Expression engine & behavior helpers

> Note: despite the historical name, this is **not only** a PCA9685 driver.  
> It is the main library used by EMOtto 3.0 firmware.

---

## Folder Layout (expected)

When included in the main EMOtto repository:

```
EMOtto-3.0/
└─ libraries/
   └─ EMOtto32_pca9685/
      ├─ src/
      ├─ library.properties
      └─ README.md
```

Alternatively you can install it as an Arduino library by copying `EMOtto32_pca9685/` into:

- **Windows:** `Documents/Arduino/libraries/`
- **Linux:** `~/Arduino/libraries/`
- **macOS:** `~/Documents/Arduino/libraries/`

---

## Basic Include

```cpp
#include <EMOtto.h>
```

---

## Hardware Notes (quick)

### PCA9685 (I2C)
- SDA: **GPIO11**
- SCL: **GPIO10**
- VCC: **3.3V**
- V+: **5V external (servo power only)**

### UART (CAM ↔ LCD board)
- CAM TX (GPIO2) → LCD RX
- CAM RX (GPIO1) ← LCD TX
- GND ↔ GND

---

## License

GPL-3.0-only. See `LICENSE`.

---

## Author / Maintainer

Marco Mortari  
https://github.com/MortariMarco
