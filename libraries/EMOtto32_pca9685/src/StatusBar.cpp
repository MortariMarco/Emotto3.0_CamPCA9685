// StatusBar.cpp  (NO TOUCH)

#include <Arduino.h>
#include <lvgl.h>
#include <string.h>

#include "StatusBar.h"
#include "Battery.h"
#include "HWCDC.h"

extern HWCDC USBSerial;

// ===== APP overlay =====
#ifndef APP_DOWNLOAD_URL
#define APP_DOWNLOAD_URL  "https://tuo-dominio.example/otto-app"
#endif

#if LV_USE_QRCODE
  #include <extra/libs/qrcode/lv_qrcode.h>
#endif

// -------------------- Widget/State --------------------
static lv_obj_t* s_bar       = nullptr;
static lv_obj_t* s_wifiIcon  = nullptr;
static lv_obj_t* s_bleIcon   = nullptr;
static lv_obj_t* s_battIcon  = nullptr;
static lv_obj_t* s_battLabel = nullptr;

static lv_obj_t* s_appOverlay = nullptr;
static lv_obj_t* s_appQr      = nullptr;
static lv_obj_t* s_appTitle   = nullptr;

static const char* s_appUrl = APP_DOWNLOAD_URL;

// Palette
static lv_color_t COL_OK, COL_OFF, COL_BLE_ON;

// -------------------- Overlay helpers --------------------
static void app_overlay_event_cb(lv_event_t* e) {
  // Se in futuro riattivi touch, questo chiude l'overlay con un tap.
  // In NO TOUCH non verrà mai chiamato, ma è innocuo.
  lv_event_code_t code = lv_event_get_code(e);
  if ((code == LV_EVENT_CLICKED || code == LV_EVENT_PRESSED || code == LV_EVENT_RELEASED) && s_appOverlay) {
    lv_obj_add_flag(s_appOverlay, LV_OBJ_FLAG_HIDDEN);
    lv_event_stop_bubbling(e);
    lv_event_stop_processing(e);
  }
}

static void ensure_app_overlay_created() {
  if (s_appOverlay) return;

  s_appOverlay = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(s_appOverlay);

  lv_disp_t* d = lv_disp_get_default();
  lv_obj_set_size(s_appOverlay, lv_disp_get_hor_res(d), lv_disp_get_ver_res(d));
  lv_obj_set_style_bg_color(s_appOverlay, lv_color_white(), 0);
  lv_obj_set_style_bg_opa(s_appOverlay, LV_OPA_COVER, 0);

  // (resta clickable per compatibilità; su NO TOUCH non cambia nulla)
  lv_obj_add_flag(s_appOverlay, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(s_appOverlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(s_appOverlay, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_add_event_cb(s_appOverlay, app_overlay_event_cb, LV_EVENT_CLICKED, NULL);

  s_appTitle = lv_label_create(s_appOverlay);
  lv_label_set_text(s_appTitle, "Scansiona per scaricare l'app");
  lv_obj_set_style_text_color(s_appTitle, lv_color_black(), 0);
  lv_obj_align(s_appTitle, LV_ALIGN_TOP_MID, 0, 14);

#if LV_USE_QRCODE
  const int QR_SIZE = 180;
  s_appQr = lv_qrcode_create(s_appOverlay, QR_SIZE, lv_color_black(), lv_color_white());
  lv_qrcode_update(s_appQr, s_appUrl, strlen(s_appUrl));
  lv_obj_center(s_appQr);
#else
  s_appQr = lv_label_create(s_appOverlay);
  lv_label_set_text_fmt(s_appQr, "%s", s_appUrl);
  lv_obj_set_style_text_color(s_appQr, lv_color_black(), 0);
  lv_obj_align(s_appQr, LV_ALIGN_CENTER, 0, 0);
#endif

  lv_obj_add_flag(s_appOverlay, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(s_appOverlay);
}

bool StatusLVGL_OverlayIsVisible() {
  ensure_app_overlay_created();
  return !lv_obj_has_flag(s_appOverlay, LV_OBJ_FLAG_HIDDEN);
}

void StatusLVGL_SetAppURL(const char* url) {
  if (!url || !*url) return;
  s_appUrl = url;

  if (!s_appOverlay) return; // verrà creato e aggiornato alla prima apertura

#if LV_USE_QRCODE
  if (s_appQr) lv_qrcode_update(s_appQr, s_appUrl, strlen(s_appUrl));
#else
  if (s_appQr) lv_label_set_text_fmt(s_appQr, "%s", s_appUrl);
#endif
}

void StatusLVGL_ToggleAppOverlay() {
  ensure_app_overlay_created();
  bool hidden = lv_obj_has_flag(s_appOverlay, LV_OBJ_FLAG_HIDDEN);
  if (hidden) {
    lv_obj_clear_flag(s_appOverlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_appOverlay);
  } else {
    lv_obj_add_flag(s_appOverlay, LV_OBJ_FLAG_HIDDEN);
  }
  if (lv_disp_get_default()) lv_refr_now(lv_disp_get_default());
}

// ==================== Status Bar ====================
void StatusLVGL_Create() {
  if (s_bar) return;

  lv_disp_t* disp = lv_disp_get_default();
  int scrW = disp ? lv_disp_get_hor_res(disp) : 280;

  // Palette
  COL_OK     = lv_color_white();
  COL_OFF    = lv_color_hex(0xB0B0B0);
  COL_BLE_ON = lv_palette_main(LV_PALETTE_BLUE);

  // Barra in basso
  s_bar = lv_obj_create(lv_scr_act());
  lv_obj_remove_style_all(s_bar);
  lv_obj_set_size(s_bar, scrW, kStatusBarHeight);
  lv_obj_align(s_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_opa(s_bar, LV_OPA_80, 0);
  lv_obj_set_style_bg_color(s_bar, lv_palette_darken(LV_PALETTE_GREY, 3), 0);
  lv_obj_set_style_border_width(s_bar, 0, 0);
  lv_obj_set_style_pad_all(s_bar, 0, 0);
  lv_obj_clear_flag(s_bar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_move_foreground(s_bar);

  // Label "App" (solo estetica)
  lv_obj_t* appLabel = lv_label_create(s_bar);
  lv_label_set_text(appLabel, "App");
  lv_obj_set_style_text_color(appLabel, lv_color_white(), 0);
  lv_obj_center(appLabel);

  // WiFi
  s_wifiIcon = lv_label_create(s_bar);
  lv_label_set_text(s_wifiIcon, LV_SYMBOL_WIFI);
  lv_obj_align(s_wifiIcon, LV_ALIGN_LEFT_MID, 22, 0);
  lv_obj_set_style_text_color(s_wifiIcon, COL_OFF, 0);

  // BLE
  s_bleIcon = lv_label_create(s_bar);
  lv_label_set_text(s_bleIcon, LV_SYMBOL_BLUETOOTH);
  lv_obj_align_to(s_bleIcon, s_wifiIcon, LV_ALIGN_OUT_RIGHT_MID, 8, 0);
  lv_obj_set_style_text_color(s_bleIcon, COL_OFF, 0);

  // Batteria
  s_battLabel = lv_label_create(s_bar);
  lv_label_set_text(s_battLabel, "100%");
  lv_obj_align(s_battLabel, LV_ALIGN_RIGHT_MID, -25, 0);
  lv_obj_set_style_text_color(s_battLabel, lv_color_white(), 0);

  s_battIcon = lv_label_create(s_bar);
  lv_label_set_text(s_battIcon, LV_SYMBOL_BATTERY_FULL);
  lv_obj_align_to(s_battIcon, s_battLabel, LV_ALIGN_OUT_LEFT_MID, -4, 0);
  lv_obj_set_style_text_color(s_battIcon, lv_color_white(), 0);
}

// -------------------- Aggiornamenti icone --------------------
void StatusLVGL_SetWiFi(bool on) {
  if (!s_wifiIcon) return;
  lv_obj_set_style_text_color(s_wifiIcon, on ? COL_OK : COL_OFF, 0);
}

void StatusLVGL_SetBLE(bool on) {
  if (!s_bleIcon) return;
  lv_obj_set_style_text_color(s_bleIcon, on ? COL_BLE_ON : COL_OFF, 0);
}

// -------------------- Batteria: init + update --------------------
void StatusLVGL_BatteryInit() {
  Battery_Init();
}

void StatusLVGL_UpdateBatteryFromADC() {
  float vbat     = Battery_ReadVoltage();
  int   pct      = Battery_EstimatePercent(vbat);
  bool  charging = Battery_IsCharging();
  StatusLVGL_SetBattery(pct, charging);
}

void StatusLVGL_SetBattery(int pct, bool charging) {
  if (!s_battIcon || !s_battLabel) return;

  if (pct < 0)   pct = 0;
  if (pct > 100) pct = 100;

  const char* sym = LV_SYMBOL_BATTERY_EMPTY;
  if      (pct >= 85) sym = LV_SYMBOL_BATTERY_FULL;
  else if (pct >= 60) sym = LV_SYMBOL_BATTERY_3;
  else if (pct >= 35) sym = LV_SYMBOL_BATTERY_2;
  else if (pct >= 15) sym = LV_SYMBOL_BATTERY_1;

  if (charging) lv_label_set_text_fmt(s_battIcon, "%s %s", LV_SYMBOL_CHARGE, sym);
  else          lv_label_set_text(s_battIcon, sym);

  lv_label_set_text_fmt(s_battLabel, "%d%%", pct);

  lv_obj_set_style_text_color(s_battIcon,  lv_color_white(), 0);
  lv_obj_set_style_text_color(s_battLabel, lv_color_white(), 0);
}

void StatusLVGL_SetVisible(bool on) {
  if (!s_bar) return;
  if (on) lv_obj_clear_flag(s_bar, LV_OBJ_FLAG_HIDDEN);
  else    lv_obj_add_flag(s_bar, LV_OBJ_FLAG_HIDDEN);
}
