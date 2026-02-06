// StatusBar.h
#ifndef STATUS_BAR_H
#define STATUS_BAR_H

#include "Battery.h"
#include <lvgl.h>

// Altezza barra: puoi override-are prima di includere questo header
#ifndef kStatusBarHeight
#define kStatusBarHeight 20
#endif

// Crea la status bar in basso
void StatusLVGL_Create();

// Aggiorna WiFi (verde se connesso) – rssi opzionale
void StatusLVGL_SetWiFi(bool connected);

// Aggiorna BLE (blu se connesso)
void StatusLVGL_SetBLE(bool connected);

// Aggiorna batteria: icona + percentuale
void StatusLVGL_BatteryInit();            // config ADC
void StatusLVGL_UpdateBatteryFromADC();   // legge ADC -> aggiorna icona+%
void StatusLVGL_SetBattery(int pct, bool charging); // già esiste: la usiamo internamente
void StatusLVGL_SetVisible(bool on);

// app
void StatusLVGL_SetAppURL(const char* url);
void StatusLVGL_ToggleAppOverlay(); 

bool StatusLVGL_OverlayIsVisible();


#endif