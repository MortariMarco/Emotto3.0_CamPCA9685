#ifndef BATTERY_H
#define BATTERY_H

#include <Arduino.h>

/*
 * Lettura batteria stile Waveshare ESP32-S3 Touch LCD 1.69"
 * - ADC su GPIO1 (ADC1_CH0) con analogReadMilliVolts()
 * - Partitore 200k (alto) / 100k (basso) → fattore ≈ 3.0
 * - Nessuna dipendenza da pin_config.h
 */

// --- PIN ADC (default Waveshare: GPIO1) ---
#ifndef BAT_ADC_PIN
#define BAT_ADC_PIN 1
#endif

// --- Partitore (default Waveshare: 200k/100k) ---
#ifndef BAT_R1
#define BAT_R1 200000.0f  // ohm (alto verso batteria)
#endif
#ifndef BAT_R2
#define BAT_R2 100000.0f  // ohm (basso verso GND)
#endif

// Fattore (se non fornito direttamente)
#ifndef BAT_DIVIDER
#define BAT_DIVIDER ((BAT_R1 + BAT_R2) / (BAT_R2)) // ~3.0
#endif

// --- Pin stato carica (opzionale). Se assente, IsCharging() restituisce false. ---
#ifndef BAT_CHG_PIN
#define BAT_CHG_PIN -1
#endif
#ifndef BAT_CHG_ACTIVE_LOW
#define BAT_CHG_ACTIVE_LOW 1    // 1 = LOW significa "in carica" (CHG_N tipico)
#endif

// ===================== API =====================
void  Battery_Init();                     // configura ADC/attenuazione e (se presente) il pin CHG
float Battery_ReadVoltage();              // Volt batteria (filtrati) = analogReadMilliVolts * BAT_DIVIDER
int   Battery_EstimatePercent(float v);   // stima % LiPo 1S (0..100)
bool  Battery_IsCharging();               // true se CHG presente e indica "in carica"

#endif // BATTERY_H
