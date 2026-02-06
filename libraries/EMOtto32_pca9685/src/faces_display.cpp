// faces_display.cpp — CLEAN (NO TOUCH, NO SWAP) — LV_COLOR_16_SWAP=0

#include <Arduino.h>
#include <lvgl.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <Wire.h>

#include "Faces.h"
#include "Arduino_GFX_Library.h"
#include "Arduino_DriveBus_Library.h"
#include "pin_config.h"
#include "wifiCam.h"
#include "HWCDC.h"
#include "SensorQMI8658.hpp"
#include "StatusBar.h"

extern HWCDC USBSerial;

// ===================== IMU =====================
SensorQMI8658 qmi;
bool gQmiPresent = false;

// Busy-guard implementati in Faces.cpp
extern void Faces_CamBusyBegin(void);
extern void Faces_CamBusyEnd(void);
extern bool Faces_CamIsBusy(void);

// wifiCam.h deve già dichiararle, ma così eviti mismatch di firma
extern bool camFetchExact(const char* url, uint8_t* dst, size_t bytes);

// ===================== LVGL tick =====================
#define LVGL_TICK_MS 2
static void lvgl_tick_cb(void* /*arg*/) { lv_tick_inc(LVGL_TICK_MS); }

// ===================== Display (bus) =====================
static Arduino_DataBus* s_bus = nullptr;

// Oggetto globale (esposto in Faces.h)
Arduino_GFX* gfx = nullptr;

// ===================== LVGL: buffer & driver =====================
static lv_disp_draw_buf_t s_draw_buf;
static lv_color_t* s_buf1 = nullptr;
static lv_color_t* s_buf2 = nullptr;
static lv_disp_drv_t s_disp_drv;
static lv_disp_t* s_disp = nullptr;

// ===================== Layout runtime =====================
static int16_t sEyesX = 0, sEyesY = 0, sMouthX = 0, sMouthY = 0;

// Canvas e buffer (PSRAM) per occhi/bocca (esposti in Faces.h)
lv_obj_t*  gEyesCanvas  = nullptr;
lv_obj_t*  gMouthCanvas = nullptr;
uint16_t*  gEyesBuf     = nullptr;
uint16_t*  gMouthBuf    = nullptr;

// Canvas Z (overlay "Zzz")
#define Z_CANVAS_W 36
#define Z_CANVAS_H 36
static lv_obj_t* gZCanvas = nullptr;
static uint16_t* gZBuf    = nullptr;

// ===================== MULTI-VARIANT CACHES =====================
#define FACES_VARIANTS_MAX 8

static uint16_t* gEyesClosedCache = nullptr;
static uint16_t* gEyesOpenCache   = nullptr; // “active copy” (veloce)
static uint16_t* gMouthCache      = nullptr; // “active copy” (veloce)

static const char* gEyesOpenUrl[FACES_VARIANTS_MAX]  = {nullptr};
static uint8_t     gEyesUrlN = 0;
static const char* gEyesClosedUrl = nullptr;

static const char* gMouthOpenUrl[FACES_VARIANTS_MAX] = {nullptr};
static uint8_t     gMouthUrlN = 0;

// Varianti in PSRAM
static uint16_t* gEyesOpenVar[FACES_VARIANTS_MAX] = {nullptr};
static uint8_t   gEyesOpenVarN = 0;

static uint16_t* gMouthVar[FACES_VARIANTS_MAX] = {nullptr};
static uint8_t   gMouthVarN = 0;

// Bocca blink cache è gestita in Faces.cpp
extern uint16_t*   gMouthBlinkCache;
extern const char* gMouthBlinkURL;

// Idempotenza preload
static uint32_t s_lastEyesSig  = 0;
static uint32_t s_lastMouthSig = 0;

static inline uint32_t fnv1a_step(uint32_t h, const char* s){
  if (!s) return h ^ 0xAA;
  while (*s) { h ^= (uint8_t)(*s++); h *= 16777619u; }
  return h ^ 0x55;
}
static uint32_t make_sig(const char* const urls[], uint8_t n, const char* extra=nullptr){
  uint32_t h = 2166136261u;
  for (uint8_t i=0;i<n;i++) h = fnv1a_step(h, urls[i]);
  if (extra) h = fnv1a_step(h, extra);
  if (h == 0) h = 1;
  return h;
}

// ===================== Flush LVGL -> TFT =====================
static void my_disp_flush(lv_disp_drv_t* disp, const lv_area_t* area, lv_color_t* color_p) {
  if (!gfx) { lv_disp_flush_ready(disp); return; }

  const uint32_t w = (uint32_t)(area->x2 - area->x1 + 1);
  const uint32_t h = (uint32_t)(area->y2 - area->y1 + 1);

  // LV_COLOR_16_SWAP=0 => buffer LVGL già corretto per draw16bitRGBBitmap
  uint16_t* src = (uint16_t*)&color_p->full;
  gfx->draw16bitRGBBitmap(area->x1, area->y1, src, w, h);

  lv_disp_flush_ready(disp);
}

// ===================== Retry helper camFetchExact =====================
static bool fetchExactRetry(const char* url, uint8_t* dst, size_t bytes,
                            int tries, uint16_t wait_ms)
{
  for (int a=1; a<=tries; ++a) {
    if (camFetchExact(url, dst, bytes)) return true;
    USBSerial.printf("[cam] fetch fail '%s' attempt %d/%d\n", url ? url : "(null)", a, tries);
    delay(wait_ms + (millis() & 0x1F));  // jitter
  }
  return false;
}

// ===================== Init Display + LVGL =====================
void Faces_InitDisplayAndLVGL() {
  // BUS / DISPLAY
  if (!s_bus) s_bus = new Arduino_ESP32SPI(LCD_DC, LCD_CS, LCD_SCK, LCD_MOSI);

  // NOTA: il parametro "true" qui spesso indica IPS/BGR: se colori invertiti,
  // la correzione è qui (non con byte-swap). Per ora lo lascio come nel tuo.
  if (!gfx)   gfx   = new Arduino_ST7789(s_bus, LCD_RST, 0, true,
                                        LCD_WIDTH, LCD_HEIGHT, 0, 20, 0, 0);

  gfx->begin();
  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, HIGH);

  // LVGL init (una volta)
  static bool s_lvglInited = false;
  if (!s_lvglInited) {
    lv_init();
    s_lvglInited = true;
  }

  // Driver display LVGL (una volta)
  static bool s_dispRegistered = false;
  if (!s_dispRegistered) {
    const uint16_t hor = gfx->width();
    const uint16_t ver = gfx->height();

    // buffer 1/4 schermo, double buffer (DMA)
    const size_t buf_px = (hor * (size_t)ver) / 4;

    if (!s_buf1) s_buf1 = (lv_color_t*)heap_caps_malloc(buf_px * sizeof(lv_color_t), MALLOC_CAP_DMA);
    if (!s_buf2) s_buf2 = (lv_color_t*)heap_caps_malloc(buf_px * sizeof(lv_color_t), MALLOC_CAP_DMA);

    if (!s_buf1 || !s_buf2) {
      USBSerial.printf("[LVGL] ❌ alloc buffer fallita (buf1=%p buf2=%p)\n", (void*)s_buf1, (void*)s_buf2);
      return;
    }

    lv_disp_draw_buf_init(&s_draw_buf, s_buf1, s_buf2, buf_px);

    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res   = hor;
    s_disp_drv.ver_res   = ver;
    s_disp_drv.flush_cb  = my_disp_flush;
    s_disp_drv.draw_buf  = &s_draw_buf;
    s_disp_drv.sw_rotate = 1;

    s_disp = lv_disp_drv_register(&s_disp_drv);
    lv_disp_set_rotation(s_disp, LV_DISP_ROT_270);

    s_dispRegistered = true;
  }

  // SFONDO
  lv_obj_t* scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, lv_color_hex(0xFFEFCA), 0);
  lv_obj_set_style_bg_opa  (scr, LV_OPA_COVER,          0);

  // Status bar
  StatusLVGL_Create();

  // Risoluzione post-rotazione
  const uint16_t scrW = lv_disp_get_hor_res(s_disp);
  const uint16_t scrH = lv_disp_get_ver_res(s_disp);

  // Layout occhi/bocca
  const int marginTopEyes = 20;
  const int spacing       = 0;

  sEyesX  = (scrW - EYES_W) / 2;
  sEyesY  = marginTopEyes;
  sMouthX = (scrW - MOUTH_W) / 2;
  sMouthY = sEyesY + EYES_H + spacing;

  if (sMouthY + MOUTH_H > (int)scrH) {
    int extra = (sMouthY + MOUTH_H) - (int)scrH;
    sEyesY  = max(10, sEyesY - extra/2);
    sMouthY = sEyesY + EYES_H + spacing;
  }

  // Tick timer LVGL (una volta)
  static esp_timer_handle_t s_lvgl_tick_timer = nullptr;
  if (!s_lvgl_tick_timer) {
    const esp_timer_create_args_t args = { .callback = &lvgl_tick_cb, .name = "lvgl_tick" };
    esp_timer_create(&args, &s_lvgl_tick_timer);
    esp_timer_start_periodic(s_lvgl_tick_timer, LVGL_TICK_MS * 1000);
  }

  // Canvas occhi/bocca
  if (!gEyesCanvas)  gEyesCanvas  = lv_canvas_create(scr);
  if (!gMouthCanvas) gMouthCanvas = lv_canvas_create(scr);

  if (!gEyesBuf)  gEyesBuf  = (uint16_t*)heap_caps_malloc((size_t)EYES_W  * EYES_H  * 2, MALLOC_CAP_SPIRAM);
  if (!gMouthBuf) gMouthBuf = (uint16_t*)heap_caps_malloc((size_t)MOUTH_W * MOUTH_H * 2, MALLOC_CAP_SPIRAM);

  if (!gEyesBuf || !gMouthBuf) {
    USBSerial.println("❌ PSRAM alloc canvas fallita");
    return;
  }

  lv_canvas_set_buffer(gEyesCanvas,  gEyesBuf,  EYES_W,  EYES_H,  LV_IMG_CF_TRUE_COLOR);
  lv_canvas_set_buffer(gMouthCanvas, gMouthBuf, MOUTH_W, MOUTH_H, LV_IMG_CF_TRUE_COLOR);

  memset(gEyesBuf,  0, (size_t)EYES_W  * EYES_H  * 2);
  memset(gMouthBuf, 0, (size_t)MOUTH_W * MOUTH_H * 2);

  lv_obj_set_pos(gEyesCanvas,  sEyesX,  sEyesY);
  lv_obj_set_pos(gMouthCanvas, sMouthX, sMouthY);

  lv_obj_clear_flag(gEyesCanvas,  LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(gMouthCanvas, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

  // Canvas Zzz
  if (!gZCanvas) gZCanvas = lv_canvas_create(scr);
  if (!gZBuf) gZBuf = (uint16_t*)heap_caps_malloc((size_t)Z_CANVAS_W * Z_CANVAS_H * 2, MALLOC_CAP_SPIRAM);
  if (!gZBuf) { USBSerial.println("❌ PSRAM alloc Z canvas fallita"); return; }

  lv_canvas_set_buffer(gZCanvas, gZBuf, Z_CANVAS_W, Z_CANVAS_H, LV_IMG_CF_TRUE_COLOR);
  memset(gZBuf, 0, (size_t)Z_CANVAS_W * Z_CANVAS_H * 2);

  lv_obj_set_pos(gZCanvas, 0, 0);
  lv_obj_clear_flag(gZCanvas, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(gZCanvas, LV_OBJ_FLAG_HIDDEN);
}

// ===================== LVGL loop =====================
void Faces_LvglLoop() {
  lv_timer_handler();
}

// ===================== PRELOAD MULTI =====================
bool Faces_PreloadEyesMulti(const char* const urlsOpen[], uint8_t nOpen, const char* urlClosed) {
  if (!urlsOpen || nOpen == 0 || !urlClosed) return false;
  if (nOpen > FACES_VARIANTS_MAX) nOpen = FACES_VARIANTS_MAX;

  const uint32_t sig = make_sig(urlsOpen, nOpen, urlClosed);
  if (sig == s_lastEyesSig && gEyesClosedCache && gEyesOpenCache && gEyesOpenVarN >= nOpen) {
    gEyesUrlN = nOpen;
    for (uint8_t i=0; i<nOpen; ++i) gEyesOpenUrl[i] = urlsOpen[i];
    gEyesClosedUrl = urlClosed;
    return true;
  }

  const size_t ebytes = (size_t)EYES_W * EYES_H * 2;

  if (!gEyesClosedCache) gEyesClosedCache = (uint16_t*)heap_caps_malloc(ebytes, MALLOC_CAP_SPIRAM);
  if (!gEyesClosedCache) { USBSerial.println("❌ PSRAM occhi-closed"); return false; }

  if (!gEyesOpenCache) gEyesOpenCache = (uint16_t*)heap_caps_malloc(ebytes, MALLOC_CAP_SPIRAM);
  if (!gEyesOpenCache) { USBSerial.println("❌ PSRAM eyes-open-active"); return false; }

  uint8_t* tmp = (uint8_t*)heap_caps_malloc(ebytes, MALLOC_CAP_SPIRAM);
  if (!tmp) { USBSerial.println("❌ PSRAM tmp eyes-multi"); return false; }

  Faces_CamBusyBegin();

  // closed
  if (!fetchExactRetry(urlClosed, tmp, ebytes, 3, 25)) {
    Faces_CamBusyEnd();
    heap_caps_free(tmp);
    USBSerial.println("❌ preload eyes-closed");
    return false;
  }
  memcpy(gEyesClosedCache, tmp, ebytes);

  // open variants
  uint8_t loaded = 0;
  for (uint8_t i=0; i<nOpen; ++i) {
    if (!urlsOpen[i]) break;

    if (!gEyesOpenVar[i]) gEyesOpenVar[i] = (uint16_t*)heap_caps_malloc(ebytes, MALLOC_CAP_SPIRAM);
    if (!gEyesOpenVar[i]) { USBSerial.println("❌ PSRAM eyes-open-variant"); break; }

    if (!fetchExactRetry(urlsOpen[i], tmp, ebytes, 3, 25)) {
      USBSerial.printf("❌ preload eyes open var %u\n", i);
      break;
    }
    memcpy(gEyesOpenVar[i], tmp, ebytes);
    loaded++;
  }

  Faces_CamBusyEnd();
  heap_caps_free(tmp);

  if (loaded == 0) return false;
  gEyesOpenVarN = loaded;

  // active copy = var0
  memcpy(gEyesOpenCache, gEyesOpenVar[0], ebytes);

  gEyesUrlN = nOpen;
  for (uint8_t i=0; i<nOpen; ++i) gEyesOpenUrl[i] = urlsOpen[i];
  gEyesClosedUrl = urlClosed;

  s_lastEyesSig = sig;
  USBSerial.printf("Preload eyes-multi: %u open + closed OK\n", (unsigned)gEyesOpenVarN);
  return true;
}

bool Faces_PreloadMouthMulti(const char* const urls[], uint8_t count) {
  if (!urls || count == 0) return false;
  if (count > FACES_VARIANTS_MAX) count = FACES_VARIANTS_MAX;

  const uint32_t sig = make_sig(urls, count, nullptr);
  if (sig == s_lastMouthSig && gMouthCache && gMouthVarN >= count) {
    gMouthUrlN = count;
    for (uint8_t i=0; i<count; ++i) gMouthOpenUrl[i] = urls[i];
    return true;
  }

  const size_t mbytes = (size_t)MOUTH_W * MOUTH_H * 2;

  if (!gMouthCache) gMouthCache = (uint16_t*)heap_caps_malloc(mbytes, MALLOC_CAP_SPIRAM);
  if (!gMouthCache) { USBSerial.println("❌ PSRAM mouth-active"); return false; }

  uint8_t* tmp = (uint8_t*)heap_caps_malloc(mbytes, MALLOC_CAP_SPIRAM);
  if (!tmp) { USBSerial.println("❌ PSRAM tmp mouth-multi"); return false; }

  Faces_CamBusyBegin();

  uint8_t loaded = 0;
  for (uint8_t i=0; i<count; ++i) {
    if (!urls[i]) break;

    if (!gMouthVar[i]) gMouthVar[i] = (uint16_t*)heap_caps_malloc(mbytes, MALLOC_CAP_SPIRAM);
    if (!gMouthVar[i]) { USBSerial.println("❌ PSRAM mouth-var"); break; }

    if (!fetchExactRetry(urls[i], tmp, mbytes, 3, 25)) {
      USBSerial.printf("❌ preload mouth var %u\n", i);
      break;
    }
    memcpy(gMouthVar[i], tmp, mbytes);
    loaded++;
  }

  Faces_CamBusyEnd();
  heap_caps_free(tmp);

  if (loaded == 0) return false;
  gMouthVarN = loaded;

  // active copy = var0
  memcpy(gMouthCache, gMouthVar[0], mbytes);

  gMouthUrlN = count;
  for (uint8_t i=0; i<count; ++i) gMouthOpenUrl[i] = urls[i];

  s_lastMouthSig = sig;
  USBSerial.printf("Preload mouth-multi: %u open OK\n", (unsigned)gMouthVarN);
  return true;
}

// ===================== SHOW from CACHE =====================
bool Faces_ShowEyesOpenURL(const char* url) {
  if (!url || !gEyesCanvas || !gEyesBuf) return false;

  for (uint8_t i=0; i<gEyesUrlN; ++i) {
    if (gEyesOpenUrl[i] && strcmp(gEyesOpenUrl[i], url) == 0 && gEyesOpenVar[i]) {
      const size_t bytes = (size_t)EYES_W * EYES_H * 2;
      memcpy(gEyesBuf, gEyesOpenVar[i], bytes);
      if (gEyesOpenCache) memcpy(gEyesOpenCache, gEyesOpenVar[i], bytes);
      lv_obj_invalidate(gEyesCanvas);
      return true;
    }
  }
  return false;
}

bool Faces_ShowEyesClosedCached() {
  if (!gEyesCanvas || !gEyesBuf || !gEyesClosedCache) return false;
  memcpy(gEyesBuf, gEyesClosedCache, (size_t)EYES_W * EYES_H * 2);
  lv_obj_invalidate(gEyesCanvas);
  return true;
}

bool Faces_ShowMouthOpenURL(const char* url) {
  if (!url || !gMouthCanvas || !gMouthBuf) return false;

  for (uint8_t i=0; i<gMouthUrlN; ++i) {
    if (gMouthOpenUrl[i] && strcmp(gMouthOpenUrl[i], url) == 0 && gMouthVar[i]) {
      const size_t bytes = (size_t)MOUTH_W * MOUTH_H * 2;
      memcpy(gMouthBuf, gMouthVar[i], bytes);
      if (gMouthCache) memcpy(gMouthCache, gMouthVar[i], bytes);
      lv_obj_invalidate(gMouthCanvas);
      return true;
    }
  }
  return false;
}

bool Faces_ShowMouthBlinkCached() {
  if (!gMouthCanvas || !gMouthBuf || !gMouthBlinkCache) return false;
  memcpy(gMouthBuf, gMouthBlinkCache, (size_t)MOUTH_W * MOUTH_H * 2);
  lv_obj_invalidate(gMouthCanvas);
  return true;
}

// ===================== DRAW veloci (cache -> canvas) =====================
bool Faces_ShowEyes(bool closed) {
  if (!gEyesCanvas || !gEyesBuf) return false;
  uint16_t* src = closed ? gEyesClosedCache : gEyesOpenCache;
  if (!src) return false;
  memcpy(gEyesBuf, src, (size_t)EYES_W * EYES_H * 2);
  lv_obj_invalidate(gEyesCanvas);
  return true;
}

bool Faces_ShowMouth() {
  if (!gMouthCanvas || !gMouthBuf || !gMouthCache) return false;
  memcpy(gMouthBuf, gMouthCache, (size_t)MOUTH_W * MOUTH_H * 2);
  lv_obj_invalidate(gMouthCanvas);
  return true;
}

bool Faces_ShowMouthFromBuffer(const uint16_t* buf) {
  if (!buf || !gMouthCanvas || !gMouthBuf) return false;
  memcpy(gMouthBuf, buf, (size_t)MOUTH_W * MOUTH_H * 2);
  lv_obj_invalidate(gMouthCanvas);
  return true;
}

// ===================== Loader diretti su canvas (blocking) =====================
bool Faces_EyesLoadURL(const char* url) {
  if (!url || !gEyesCanvas || !gEyesBuf) return false;

  const char* curOpen = nullptr; const char* curClosed = nullptr;
  Faces_GetCurrentEyesURLs(&curOpen, &curClosed);

  const size_t bytes = (size_t)EYES_W * EYES_H * 2;

  // se coincide con l'open attuale e abbiamo cache active
  if (curOpen && gEyesOpenCache && strcmp(curOpen, url) == 0) {
    memcpy(gEyesBuf, gEyesOpenCache, bytes);
    lv_obj_invalidate(gEyesCanvas);
    return true;
  }

  uint8_t* tmp = (uint8_t*)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
  if (!tmp) return false;

  Faces_CamBusyBegin();
  const bool ok = fetchExactRetry(url, tmp, bytes, 2, 20);
  Faces_CamBusyEnd();

  if (!ok) { heap_caps_free(tmp); return false; }

  memcpy(gEyesBuf, tmp, bytes);
  heap_caps_free(tmp);

  lv_obj_invalidate(gEyesCanvas);
  return true;
}

bool Faces_MouthLoadURL(const char* url) {
  if (!url || !gMouthCanvas || !gMouthBuf) return false;

  const char* cur = Faces_GetCurrentMouthURL();
  const size_t bytes = (size_t)MOUTH_W * MOUTH_H * 2;

  if (cur && gMouthCache && strcmp(cur, url) == 0) {
    memcpy(gMouthBuf, gMouthCache, bytes);
    lv_obj_invalidate(gMouthCanvas);
    return true;
  }

  uint8_t* tmp = (uint8_t*)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
  if (!tmp) return false;

  Faces_CamBusyBegin();
  const bool ok = fetchExactRetry(url, tmp, bytes, 2, 20);
  Faces_CamBusyEnd();

  if (!ok) { heap_caps_free(tmp); return false; }

  memcpy(gMouthBuf, tmp, bytes);
  heap_caps_free(tmp);

  lv_obj_invalidate(gMouthCanvas);
  return true;
}

// ===================== Z canvas draw =====================
void Faces_DrawBinAt(const char* filename, int16_t x, int16_t y, uint8_t w, uint8_t h) {
  if (!filename || !gZCanvas || !gZBuf) return;
  if (!w || !h) return;
  if (w > Z_CANVAS_W || h > Z_CANVAS_H) {
    USBSerial.println("❌ Z bin troppo grande per il canvas");
    return;
  }

  const size_t bytes = (size_t)w * h * 2;

  uint8_t* tmp = (uint8_t*)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
  if (!tmp) { USBSerial.println("❌ PSRAM tmp Zzz"); return; }

  Faces_CamBusyBegin();
  const bool ok = fetchExactRetry(filename, tmp, bytes, 2, 20);
  Faces_CamBusyEnd();

  if (!ok) {
    USBSerial.printf("❌ fetch Zzz '%s'\n", filename);
    heap_caps_free(tmp);
    return;
  }

  // pulisci sfondo canvas Z con lo stesso bg del face
  lv_canvas_fill_bg(gZCanvas, lv_color_hex(0xFFEFCA), LV_OPA_COVER);

  // copia solo la porzione w*h dentro il canvas 36x36
  uint16_t* p = (uint16_t*)tmp;
  for (uint8_t row=0; row<h; ++row) {
    uint16_t* dstRow = gZBuf + row * Z_CANVAS_W;
    uint16_t* srcRow = p     + row * w;
    memcpy(dstRow, srcRow, (size_t)w * 2);
  }

  heap_caps_free(tmp);

  lv_obj_set_pos(gZCanvas, x, y);
  lv_obj_clear_flag(gZCanvas, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(gZCanvas);
  lv_obj_invalidate(gZCanvas);
}

void Faces_HideZCanvas() {
  if (!gZCanvas) return;
  lv_obj_add_flag(gZCanvas, LV_OBJ_FLAG_HIDDEN);
}

// ===================== IMU init (stile Waveshare) =====================
bool initIMU_WaveshareStyle() {
  const uint8_t IMU_ADDR = 0x6B;

  // power settle
  delay(50);

  bool ok = false;
  for (int i = 0; i < 8 && !ok; ++i) {
    ok = qmi.begin(Wire, IMU_ADDR, IIC_SDA, IIC_SCL);
    if (!ok) delay(80);
  }

  if (!ok) {
    gQmiPresent = false;
    USBSerial.println("[IMU] QMI8658 init FAIL (0x6B). Continuo senza IMU.");
    return false;
  }

  qmi.configAccelerometer(SensorQMI8658::ACC_RANGE_4G,
                          SensorQMI8658::ACC_ODR_1000Hz,
                          SensorQMI8658::LPF_MODE_0,
                          true);

  qmi.configGyroscope(SensorQMI8658::GYR_RANGE_64DPS,
                      SensorQMI8658::GYR_ODR_896_8Hz,
                      SensorQMI8658::LPF_MODE_3,
                      true);

  qmi.enableGyroscope();
  qmi.enableAccelerometer();

  gQmiPresent = true;
  USBSerial.println("[IMU] QMI8658 OK (stile Waveshare)");
  return true;
}


