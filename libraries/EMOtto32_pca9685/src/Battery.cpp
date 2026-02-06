#include "Battery.h"

// Parametri di smoothing (overridable se vuoi via -D o prima dell'include)
#ifndef BATTERY_SAMPLES
#define BATTERY_SAMPLES 8      // numero di letture per media
#endif

#ifndef BATTERY_ALPHA
#define BATTERY_ALPHA   0.20f  // IIR: 20% misura nuova
#endif

static float s_vFilt = 0.0f;
static bool  s_vInit = false;

void Battery_Init() {
  // ADC setup in stile Waveshare
  analogReadResolution(12);
  analogSetPinAttenuation(BAT_ADC_PIN, ADC_11db);  // ~3.3V full-scale su ESP32-S3

#if (defined(BAT_CHG_PIN) && (BAT_CHG_PIN >= 0))
  pinMode(BAT_CHG_PIN, BAT_CHG_ACTIVE_LOW ? INPUT_PULLUP : INPUT);
#endif

  s_vInit = false;
}

float Battery_ReadVoltage() {
  // Media di più letture in mV sul pin del partitore
  long acc_mv = 0;
  for (int i = 0; i < BATTERY_SAMPLES; ++i) {
    acc_mv += analogReadMilliVolts(BAT_ADC_PIN);   // mV lato pin
  }
  const float v_pin  = (acc_mv / (float)BATTERY_SAMPLES) / 1000.0f; // Volt al pin
  const float v_batt = v_pin * BAT_DIVIDER;                          // Volt batteria

  if (!s_vInit) {
    s_vFilt = v_batt;
    s_vInit = true;
  } else {
    s_vFilt = s_vFilt * (1.0f - BATTERY_ALPHA) + v_batt * BATTERY_ALPHA;
  }
  return s_vFilt;
}

int Battery_EstimatePercent(float v) {
  // Stima SoC LiPo 1S pratica (open-circuit). Clamp 3.30..4.20V + interp. lineare a tratti.
  if (v <= 3.30f) return 0;
  if (v >= 4.20f) return 100;

  // tabella semplice (puoi calibrarla sulla tua cella)
  static const float pts[][2] = {
    {3.30f,  0}, {3.50f, 10}, {3.70f, 45}, {3.80f, 60},
    {3.90f, 75}, {4.00f, 85}, {4.10f, 93}, {4.20f,100}
  };
  const int N = sizeof(pts) / sizeof(pts[0]);

  for (int i = 1; i < N; ++i) {
    if (v <= pts[i][0]) {
      const float v0 = pts[i-1][0], p0 = pts[i-1][1];
      const float v1 = pts[i][0],   p1 = pts[i][1];
      const float t  = (v - v0) / (v1 - v0);
      int p = (int) lroundf(p0 + t * (p1 - p0));
      if (p < 0) p = 0; if (p > 100) p = 100;
      return p;
    }
  }
  return 100;
}

bool Battery_IsCharging() {
#if (defined(BAT_CHG_PIN) && (BAT_CHG_PIN >= 0))
  const int lvl = digitalRead(BAT_CHG_PIN);
  return BAT_CHG_ACTIVE_LOW ? (lvl == LOW) : (lvl == HIGH);
#else
  // Se non c'è pin CHG in pin_config.h, ritorna false come nell'esempio Arduino Waveshare.
  return false;
#endif
}

