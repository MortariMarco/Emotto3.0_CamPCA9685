//----vers 10.4 – aggiornato per espressioni coerenti + BLE App (expr/vol/move/speed)
#include <EMOtto.h>
Otto Otto;

#include <lvgl.h>
#include <Wire.h>
#include <memory>

#include <Adafruit_PWMServoDriver.h>
#include "HWCDC.h"
#include "esp_log.h"
#include "Bluetooth.h"
#include "wifiCam.h"
#include "Faces.h"
#include "pin_config.h"
#include "Arduino_GFX_Library.h"
#include "Arduino_DriveBus_Library.h"
#include "DFRobotDFPlayerMini.h"
#include <Adafruit_VL53L0X.h>
#include "StatusBar.h"
#include "espressioni.h"
#include "SensorQMI8658.hpp"
#include "ExpressionsCmd.h"   // gestore comandi centralizzato
#include "ExprEngine.h"
#include <esp_system.h>

HWCDC USBSerial;
DFRobotDFPlayerMini dfplayer;                 // <-- NON cambiato nome
Adafruit_VL53L0X vl53l0x = Adafruit_VL53L0X();

const char* ssid = "OttoCamServer";
const char* password = "12345678";

// ==== PCA9685 (Oscillator.cpp lo usa) ====
Adafruit_PWMServoDriver g_pca9685 = Adafruit_PWMServoDriver(0x40);
#define SERVO_YL 0
#define SERVO_YR 1
#define SERVO_RL 2
#define SERVO_RR 3
#define BUZZER_PIN 42

// -------------------- UTIL --------------------
void i2cScanner() {
  byte error, address;
  int nDevices = 0;
  USBSerial.println("Scanning I2C bus...");
  for (address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    error = Wire.endTransmission();
    if (error == 0) {
      USBSerial.print("Device found at 0x");
      USBSerial.println(address, HEX);
      nDevices++;
    }
  }
  if (nDevices == 0) USBSerial.println("No I2C devices found\n");
}

// -------------------- Gesture async --------------------
static TaskHandle_t gGestureTask = nullptr;

static void gestureTask(void* arg) {
  int g = (int)(intptr_t)arg;
  Otto.playGesture(g);
  gGestureTask = nullptr;
  vTaskDelete(nullptr);
}

static inline void StartGestureAsync(int gesture) {
  if (gGestureTask) return;
  xTaskCreatePinnedToCore(gestureTask, "gesture", 4096,
                          (void*)(intptr_t)gesture, 1, &gGestureTask, 0);
}

// -------------------- SETUP --------------------
void setup() {
  esp_log_level_set("*", ESP_LOG_NONE);

  USBSerial.begin(115200);
  USBSerial.println("EMOtto con PCA9685");

  if (psramInit()) USBSerial.println("✅ PSRAM OK");
  else             USBSerial.println("❌ PSRAM non disponibile!");

  // ✅ Alimentazione IMU
  pinMode(38, OUTPUT);
  digitalWrite(38, HIGH);
  delay(100);

  Wire.begin(IIC_SDA, IIC_SCL);
  Wire.setTimeOut(50);
  Wire.setClock(100000);       // 100k per init

  initIMU_WaveshareStyle();

  Wire.setClock(400000);       // poi puoi tornare a 400k

  USBSerial.println("I2C scan BEFORE DFPlayer");
  i2cScanner();

  // --- VL53L0X ---
  delay(10);
  if (!vl53l0x.begin()) USBSerial.println("VL53L0X non trovato!");
  else                  USBSerial.println("VL53L0X inizializzato correttamente!");

  // --- Display + LVGL ---
  Faces_InitDisplayAndLVGL();
  delay(50);

  // --- CAM via UART ---
  wifiCamBeginUart(3, 2, 2000000);
  WaitCamReady(1200);

  // --- WiFi status (facoltativo) ---
  initWiFiCam(ssid, password);
  StatusLVGL_SetWiFi(wifiIsConnected());

  // --- Inizializzazione comandi (serial/BLE ecc.) ---
  ExpressionsCmd_Init(&USBSerial);

  // --- DFPlayer ---
  Serial2.begin(9600, SERIAL_8N1, 17, 18);
  if (!dfplayer.begin(Serial2, false, true)) {
    USBSerial.println("DFPlayer non trovato!");
  } else {
    dfplayer.setTimeOut(100);
    dfplayer.outputDevice(DFPLAYER_DEVICE_SD);
    dfplayer.volume(18);
    USBSerial.println("DFPlayer Pronto!");
  }

  // --- PCA9685 / BLE / Otto ---
  g_pca9685.begin();
  g_pca9685.setPWMFreq(60);

  initBLE();     // <-- BLE Nordic UART (app)

  Otto.init(SERVO_YL, SERVO_YR, SERVO_RL, SERVO_RR, false, BUZZER_PIN);
  Otto.sing(S_connection);
  Otto.home();
  delay(20);

  // --- Status bar ---
  StatusLVGL_SetWiFi(false);
  StatusLVGL_BatteryInit();

  // --- Espressioni ---
  Expressions_Init();
  Expressions_SetActive(ExprKind::Natural);
  Faces_BlinkEnable(true, true);

  // --- Aura boot non bloccante ---
  Aura_BootBegin();
}

// -------------------- LOOP --------------------
void loop() {
  unsigned long now = millis();

  // BLE: callback RX + movimento continuo
  loopBLE();                 // compat
  Ble_UpdateWalking(now);     // <-- AGGIUNTA: cammina/gira finché app comanda

  // CAM
  wifiCamPump(ssid);

  // --- Display / LVGL / Faces ---
  if (!camViewIsOn()) {
    Faces_LvglLoop();
    updateFaces(now);
    Enroll_Tick_Display(now);
  } else {
    camViewTick(now);
  }

  // --- Engine espressioni + sync audio/bocca ---
  Expressions_Update(now);
  Faces_SyncUpdate(now);

  // --- BOOT AURA non bloccante + start intro ---
  static bool sBootIntroStarted = false;
  Aura_BootTick(now);

  if (!sBootIntroStarted && !Aura_BootIsRunning()) {
    sBootIntroStarted = true;

    // parlato + movimento (usa il tuo manager)
    Expressions_PlayVariant(ExprKind::Natural, 1);
    StartGestureAsync(OttoHappy);
  }

  // --- Idle/Sleep/Zzz ---
  Expressions_CheckIdle(now);
  Expressions_DrawZzzOverlay();

  // --- Comandi / IMU / Recognition ---
  ExpressionsCmd_Poll();
  
  handleIMUOnly(now);
  checkFaceRecognition(now);

  // --- Status bar ---
  static unsigned long lastStatusTs = 0;
  if (now - lastStatusTs > 5000) {
    StatusLVGL_UpdateBatteryFromADC();
    StatusLVGL_SetWiFi(wifiIsConnected());
    StatusLVGL_SetBLE(bleIsConnected());
    lastStatusTs = now;
  }

  delay(5);
}



