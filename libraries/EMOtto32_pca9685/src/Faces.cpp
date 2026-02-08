// Faces.cpp — CLEAN DEFINITIVA (blink unificato FaceBlink, cache-first, NO TOUCH)

#include <Arduino.h>
#include <lvgl.h>
#include <esp_heap_caps.h>
#include <Wire.h>
#include <vector>

#include "Faces.h"
#include "FaceBlink.h"
#include "wifiCam.h"
#include "HWCDC.h"
#include "SensorQMI8658.hpp"
#include "DFRobotDFPlayerMini.h"
#include "espressioni.h"

extern HWCDC USBSerial;
extern DFRobotDFPlayerMini dfplayer;

// IMU (definite in faces_display.cpp)
extern SensorQMI8658 qmi;
extern bool gQmiPresent;

// Low-level draw (da faces_display.cpp)
extern bool Faces_EyesLoadURL (const char* url);
extern bool Faces_MouthLoadURL(const char* url);
extern bool Faces_ShowMouthFromBuffer(const uint16_t* buf);

extern bool Faces_ShowEyesOpenURL(const char* url);
extern bool Faces_ShowEyesClosedCached();
extern bool Faces_ShowMouthOpenURL(const char* url);
extern bool Faces_ShowMouthBlinkCached();

extern bool Faces_PreloadEyesMulti (const char* const urlsOpen[], uint8_t nOpen, const char* urlClosed);
extern bool Faces_PreloadMouthMulti(const char* const urls[],     uint8_t n);

// wifiCam.h deve già dichiararle, ma così eviti sorprese di firma
extern bool camFetchExact(const char* url, uint8_t* dst, size_t bytes);
extern bool camWho(String& out);

// se vuoi invalidare canvas bocca (definito in faces_display.cpp)
extern lv_obj_t* gMouthCanvas;

// Se hai enroll display attivo
extern bool Enroll_IsActive_Display();

// forward locale (serve perché Faces_PreloadMouthBlink lo usa)
static bool fetchExactRetry(const char* url, uint8_t* dst, size_t bytes,
                            int tries = 3, uint16_t wait_ms = 30);

// =============================================================================
// CAM busy guard (globale progetto)
// =============================================================================
static volatile bool sCamBusy = false;
void Faces_CamBusyBegin(void) { sCamBusy = true; }
void Faces_CamBusyEnd(void)   { sCamBusy = false; }
bool Faces_CamIsBusy(void)    { return sCamBusy; }

// =============================================================================
// URL runtime correnti (stato “tema”)
// =============================================================================
static const char* gEyesOpenURL   = nullptr;
static const char* gEyesClosedURL = nullptr;
static const char* gMouthURL      = nullptr;

// Blink mouth (cache gestita qui)
const char*  gMouthBlinkURL   = nullptr;
uint16_t*    gMouthBlinkCache = nullptr; // cache allocata in Faces_PreloadMouthBlink (faces_display o qui)

// Getter pubblici
void Faces_GetCurrentEyesURLs(const char** outOpen, const char** outClosed) {
  if (outOpen)   *outOpen   = gEyesOpenURL;
  if (outClosed) *outClosed = gEyesClosedURL;
}
const char* Faces_GetCurrentMouthURL() { return gMouthURL; }
const char* Faces_GetCurrentMouthBlinkURL() { return gMouthBlinkURL; }

// =============================================================================
// Setter robusti (strcmp, non pointer compare)
// =============================================================================
void setEyesURLs(const char* urlOpen, const char* urlClosed) {
  if (!urlOpen || !*urlOpen || !urlClosed || !*urlClosed) return;

  if (gEyesOpenURL && gEyesClosedURL &&
      strcmp(gEyesOpenURL, urlOpen) == 0 &&
      strcmp(gEyesClosedURL, urlClosed) == 0) return;

  gEyesOpenURL   = urlOpen;
  gEyesClosedURL = urlClosed;
}

void setMouthURL(const char* url) {
  if (!url || !*url) return;
  if (gMouthURL && strcmp(gMouthURL, url) == 0) return;
  gMouthURL = url;
}

void setMouthBlinkURL(const char* url) {
  if (!url || !*url) return;
  if (gMouthBlinkCache && gMouthBlinkURL && strcmp(gMouthBlinkURL, url) == 0) return;

  if (Faces_PreloadMouthBlink(url)) {
    gMouthBlinkURL = url;
  }
}



bool Faces_BlinkSetAssets_C(const char* const eyesOpen[],  uint8_t nEyes,
                            const char*       eyesClosed,
                            const char* const mouthOpen[], uint8_t nMouth,
                            const char*       mouthBlink,
                            bool drawFirstFrame,
                            bool enableEyesBlink,
                            bool enableMouthBlink,
                            bool resetPhases)
{
  return Faces_BlinkSetAssetsRaw(
    eyesOpen, nEyes,
    eyesClosed,
    mouthOpen, nMouth,
    mouthBlink,
    drawFirstFrame,
    enableEyesBlink,
    enableMouthBlink,
    resetPhases
  );
}

bool Faces_BlinkSetAssets(std::initializer_list<const char*> eyesOpen,
                          const char* eyesClosed,
                          std::initializer_list<const char*> mouthOpen,
                          const char* mouthBlink,
                          bool drawFirstFrame,
                          bool enableEyesBlink,
                          bool enableMouthBlink,
                          bool resetPhases)
{
  // Copia in buffer locali (max 8)
  const char* e[8]; uint8_t ne = 0;
  for (auto p : eyesOpen) { if (ne < 8) e[ne++] = p; }

  const char* m[8]; uint8_t nm = 0;
  for (auto p : mouthOpen) { if (nm < 8) m[nm++] = p; }

  return Faces_BlinkSetAssetsRaw(
    e, ne, eyesClosed,
    m, nm, mouthBlink,
    drawFirstFrame, enableEyesBlink, enableMouthBlink, resetPhases
  );
}

// =============================================================================
// File callbacks init (UNA SOLA VOLTA) — cache-first + fallback prudente
// =============================================================================
static void InitBlinkFileCallbacksOnce() {
  static bool sInited = false;
  if (sInited) return;

  FaceBlink::SetFileCallbacks(
    // Eyes open
    [](const char* p){
      if (!p) return;
      if (Faces_ShowEyesOpenURL(p)) return;
      if (!Faces_CamIsBusy()) (void)Faces_EyesLoadURL(p);
    },
    // Eyes closed
    [](const char* /*p*/){
      (void)Faces_ShowEyesClosedCached();
    },
    // Mouth open
    [](const char* p){
      if (!p) return;
      if (Faces_ShowMouthOpenURL(p)) return;
      if (!Faces_CamIsBusy()) (void)Faces_MouthLoadURL(p);
    },
    // Mouth blink
    [](const char* /*p*/){
      (void)Faces_ShowMouthBlinkCached();
    }
  );

  sInited = true;
}

bool WaitCamReady(unsigned long timeoutMs) {
  uint32_t t0 = millis();
  while ((uint32_t)(millis() - t0) < (uint32_t)timeoutMs) {
    String who;
    if (camWho(who)) {
      USBSerial.printf("[CAM] ready: '%s'\n", who.c_str());
      return true;
    }
    delay(40);
  }
  USBSerial.println("[CAM] not ready (timeout) — continuing");
  return false;
}

// =============================================================================
// Blink API
// =============================================================================
bool Faces_PreloadMouthBlink(const char* url) {
  if (!url || !*url) return false;

  const size_t bytes = (size_t)MOUTH_W * MOUTH_H * 2;

  // Se già in cache, skip
  if (gMouthBlinkCache && gMouthBlinkURL && strcmp(gMouthBlinkURL, url) == 0) return true;

  if (!gMouthBlinkCache) {
    gMouthBlinkCache = (uint16_t*)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
    if (!gMouthBlinkCache) {
      USBSerial.println("❌ PSRAM alloc bocca-blink fallita");
      return false;
    }
  }

  uint8_t* tmp = (uint8_t*)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
  if (!tmp) {
    USBSerial.println("❌ PSRAM alloc tmp bocca-blink fallita");
    return false;
  }

  Faces_CamBusyBegin();
  bool ok = fetchExactRetry(url, tmp, bytes, 3, 25);
  Faces_CamBusyEnd();

#if LV_COLOR_16_SWAP
  if (ok) {
    uint16_t* p = (uint16_t*)tmp;
    for (size_t i=0; i<(size_t)MOUTH_W*MOUTH_H; ++i) {
      uint16_t v = p[i];
      p[i] = (uint16_t)((v<<8) | (v>>8));
    }
    memcpy(gMouthBlinkCache, p, bytes);
  }
#else
  if (ok) memcpy(gMouthBlinkCache, tmp, bytes);
#endif

  free(tmp);

if (ok) return true;

  USBSerial.printf("❌ preload mouth-blink FAIL '%s'\n", url);
  return false;
}


bool Faces_BlinkSetAssetsRaw(
    const char* const* eyesOpen,  uint8_t nEyes,
    const char*        eyesClosed,
    const char* const* mouthOpen, uint8_t nMouth,
    const char*        mouthBlink,
    bool drawFirstFrame,
    bool enableEyesBlink,
    bool enableMouthBlink,
    bool resetPhases)
{
  if (!eyesOpen || nEyes == 0 || !eyesClosed || !*eyesClosed) return false;
  if (!mouthOpen || nMouth == 0 || !mouthBlink || !*mouthBlink) return false;

  // 1) stato URL correnti (primo frame come "attivo")
  setEyesURLs(eyesOpen[0], eyesClosed);
  setMouthURL(mouthOpen[0]);
  setMouthBlinkURL(mouthBlink);

  // 2) preload multi (cache PSRAM)
  (void)Faces_PreloadEyesMulti (eyesOpen,  nEyes,  eyesClosed);
  (void)Faces_PreloadMouthMulti(mouthOpen, nMouth);

  // 3) callback + set varianti al motore
  InitBlinkFileCallbacksOnce();
  FaceBlink::SetEyesVariants (eyesOpen,  nEyes,  eyesClosed);
  FaceBlink::SetMouthVariants(mouthOpen, nMouth, mouthBlink);

  // 4) enable e reset
  FaceBlink::Enable(enableEyesBlink, enableMouthBlink);
  if (resetPhases) FaceBlink::ResetPhases(millis());

  // 5) draw first frame (cache-first)
  if (drawFirstFrame) {
    (void)Faces_ShowEyesOpenURL(eyesOpen[0]);
    (void)Faces_ShowMouthOpenURL(mouthOpen[0]);
  }

  return true;
}

void Faces_BlinkEnable(bool eyes, bool mouth) {
  FaceBlink::Enable(eyes, mouth);
}

void Faces_SetBlinkParamsEyes(uint16_t openMin, uint16_t openMax,
                              uint16_t blinkMin, uint16_t blinkMax,
                              uint8_t  stayPct, uint8_t  randPct,
                              uint8_t  doubleBlinkPct)
{
  FaceBlink::BlinkParams p{ openMin, openMax, blinkMin, blinkMax, stayPct, randPct, doubleBlinkPct };
  FaceBlink::SetEyesParams(p);
}

void Faces_SetBlinkParamsMouth(uint16_t openMin, uint16_t openMax,
                               uint16_t blinkMin, uint16_t blinkMax,
                               uint8_t  stayPct, uint8_t  randPct,
                               uint8_t  doubleBlinkPct)
{
  FaceBlink::BlinkParams p{ openMin, openMax, blinkMin, blinkMax, stayPct, randPct, doubleBlinkPct };
  FaceBlink::SetMouthParams(p);
}

// =============================================================================
// Mouth sequence (parlato)
// =============================================================================
static const int MOUTH_SEQ_MAX_FRAMES = 8;
static uint16_t* gMouthSeqCache[MOUTH_SEQ_MAX_FRAMES] = {nullptr};
static uint8_t   gMouthSeqCount   = 0;
static uint8_t   gMouthSeqIndex   = 0;
static bool      gMouthSeqActive  = false;
static int       gMouthSeqLoops   = 0;         // -1 infinito
static uint16_t  gMouthSeqFrameMs = 100;
static unsigned long gMouthSeqNextTs = 0;

// Armed start (offset)
static bool          gTalkAwaitArmed   = false;
static uint16_t      gTalkAwaitFrameMs = 0;
static int           gTalkAwaitLoops   = 0;
static unsigned long gTalkAwaitFireTs  = 0;

// Boot talk (audio offset)
static int16_t       sBootTrackArm     = -1;
static uint32_t      sBootAudioStartAt = 0;
static unsigned long sBootStopAt       = 0;

// Retry helper locale
static bool fetchExactRetry(const char* url, uint8_t* dst, size_t bytes,
                            int tries, uint16_t wait_ms)
{
  for (int a=1; a<=tries; ++a) {
    if (camFetchExact(url, dst, bytes)) return true;
    USBSerial.printf("[cam] fetch fail '%s' attempt %d/%d\n", url?url:"(null)", a, tries);
    delay(wait_ms + (millis() & 0x1F));
  }
  return false;
}


bool Faces_PreloadMouthSequence(const char* urls[], int count) {
  if (!urls || count <= 0) return false;
  if (count > MOUTH_SEQ_MAX_FRAMES) count = MOUTH_SEQ_MAX_FRAMES;

  const size_t bytes = (size_t)MOUTH_W * MOUTH_H * 2;
  bool allOK = true;

  Faces_CamBusyBegin();
  for (int i=0; i<count; ++i) {
    if (!urls[i] || !*urls[i]) { allOK = false; break; }

    if (!gMouthSeqCache[i]) {
      gMouthSeqCache[i] = (uint16_t*)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
      if (!gMouthSeqCache[i]) { USBSerial.println("❌ PSRAM alloc mouth-seq frame"); allOK = false; break; }
    }

    uint8_t* tmp = (uint8_t*)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
    if (!tmp) { USBSerial.println("❌ PSRAM alloc tmp mouth-seq"); allOK = false; break; }

    bool ok = fetchExactRetry(urls[i], tmp, bytes, 3, 25);
#if LV_COLOR_16_SWAP
    if (ok) {
      uint16_t* p = (uint16_t*)tmp;
      for (size_t k=0;k<(size_t)MOUTH_W*MOUTH_H;k++){ uint16_t v=p[k]; p[k]=(uint16_t)((v<<8)|(v>>8)); }
      memcpy(gMouthSeqCache[i], p, bytes);
    }
#else
    if (ok) memcpy(gMouthSeqCache[i], tmp, bytes);
#endif
    free(tmp);

    if (!ok) { USBSerial.printf("❌ preload mouth-seq frame %d\n", i); allOK = false; break; }
  }
  Faces_CamBusyEnd();

  for (int i=count; i<MOUTH_SEQ_MAX_FRAMES; ++i) {
    if (gMouthSeqCache[i]) { free(gMouthSeqCache[i]); gMouthSeqCache[i] = nullptr; }
  }

  if (allOK) {
    gMouthSeqCount = (uint8_t)count;
    USBSerial.printf("Mouth-seq preload OK: %d frame\n", count);
  }
  return allOK;
}

void Faces_StartMouthSequence(uint16_t frameMs, int loops) {
  if (gMouthSeqCount == 0) return;

  // lock eyes open during talk
  FaceBlink::LockEyes(true);
  if (gEyesOpenURL) {
    if (!Faces_ShowEyesOpenURL(gEyesOpenURL)) {
      if (!Faces_CamIsBusy()) (void)Faces_EyesLoadURL(gEyesOpenURL);
    }
  }

  gMouthSeqFrameMs = (frameMs == 0) ? 80 : frameMs;
  gMouthSeqLoops   = (loops == 0) ? 1 : loops;
  gMouthSeqActive  = true;

  gMouthSeqIndex = 0;
  Faces_ShowMouthFromBuffer(gMouthSeqCache[0]);
  gMouthSeqIndex = (gMouthSeqCount > 1) ? 1 : 0;

  uint16_t kick = (gMouthSeqFrameMs > 60) ? 60 : gMouthSeqFrameMs;
  gMouthSeqNextTs = millis() + kick;
}

void Faces_StopMouthSequence() {
  gMouthSeqActive = false;
  gMouthSeqLoops  = 0;
  gMouthSeqIndex  = 0;
  gMouthSeqNextTs = 0;

  FaceBlink::LockEyes(false);
  FaceBlink::Enable(true, true);
  FaceBlink::ResetPhases(millis());

// ✅ FORZA subito la bocca idle (smile) invece di restare sull'ultimo frame talk
  if (gMouthURL) {
    (void)Faces_ShowMouthOpenURL(gMouthURL);   // cache-first
  } else {
    Faces_ShowMouth(); // fallback: ridisegna bocca con logica attuale
  }
  if (gMouthCanvas) lv_obj_invalidate(gMouthCanvas);
}

void Faces_ArmMouthSequenceAfterDelay(uint16_t frameMs, int loops, uint16_t delayMs) {
  gTalkAwaitArmed   = true;
  gTalkAwaitFrameMs = (frameMs == 0) ? 80 : frameMs;
  gTalkAwaitLoops   = (loops == 0) ? 1 : loops;
  gTalkAwaitFireTs  = millis() + delayMs;
}

// BootTalk helpers
static uint16_t LoopsFromDuration(uint16_t frameMs, uint8_t frameCount, uint32_t ms) {
  if (!frameMs || !frameCount) return 1;
  uint32_t cycle = (uint32_t)frameMs * frameCount;
  uint32_t L = (ms + cycle/2) / cycle;
  if (L == 0) L = 1;
  if (L > 60000) L = 60000;
  return (uint16_t)L;
}

void Faces_BootTalkArm(uint8_t track, uint16_t frameMs,
                       uint8_t frameCount, uint16_t offsetMs,
                       uint32_t audioMs)
{
  const uint32_t now = millis();
  const uint16_t loops = LoopsFromDuration(frameMs, frameCount, audioMs);

  if ((int16_t)offsetMs >= 0) {
    Faces_StartMouthSequence(frameMs, loops);
    sBootTrackArm     = (int16_t)track;
    sBootAudioStartAt = now + (uint32_t)offsetMs;
    sBootStopAt       = sBootAudioStartAt + audioMs + 50;
  } else {
    dfplayer.play(track);
    Faces_ArmMouthSequenceAfterDelay(frameMs, loops, (uint16_t)(-offsetMs));
    sBootTrackArm     = -1;
    sBootAudioStartAt = 0;
    sBootStopAt       = now + audioMs + 50;
  }
}

void Faces_BootTalkBlocking(uint8_t track, uint16_t frameMs,
                            uint8_t frameCount, uint16_t offsetMs,
                            uint32_t audioMs)
{
  uint16_t loops = LoopsFromDuration(frameMs, frameCount, audioMs);
  Faces_StartMouthSequence(frameMs, loops);

  unsigned long t0 = millis();
  while ((long)(millis() - t0) < (long)offsetMs) {
    unsigned long now = millis();
    updateFaces(now);
    Faces_LvglLoop();
    delay(5);
  }
  dfplayer.play(track);
  sBootStopAt = millis() + audioMs + 60;
}

void Faces_SyncUpdate(unsigned long now) {
  // audio differito (mouth-first)
  if (sBootAudioStartAt && (long)(now - sBootAudioStartAt) >= 0) {
    if (sBootTrackArm >= 0) dfplayer.play((uint8_t)sBootTrackArm);
    sBootAudioStartAt = 0;
    sBootTrackArm     = -1;
  }

  // start sequenza differito
  if (gTalkAwaitArmed && (long)(now - gTalkAwaitFireTs) >= 0) {
    Faces_StartMouthSequence(gTalkAwaitFrameMs, gTalkAwaitLoops);
    gTalkAwaitArmed = false;
  }

  // stop stimato
  if (sBootStopAt && (long)(now - sBootStopAt) >= 0) {
    Faces_StopMouthSequence();
    sBootStopAt = 0;
    FaceBlink::Enable(true, true);
    FaceBlink::ResetPhases(millis());
    
  }
}

// =============================================================================
// Worried (IMU tilt)
// =============================================================================
static bool          gWorriedMode  = false;
static unsigned long gWorriedStart = 0;
static const unsigned long gWorriedDuration = 2000;

// worried assets
static const char* wOpen   = "occhi_preocc.bin";
static const char* wClosed = "occhioDXchiuso.bin";
static const char* wMouth  = "bocca_preoccu.bin"; // se vuoi blink diverso, metti un file chiuso

void Faces_DeferWhoNow(); // forward (WHO debounce)

void handleIMUOnly(unsigned long now) {
  if (!gQmiPresent) return;

  // Campionamento a tempo (più robusto di getDataReady)
  static unsigned long imuLast = 0;
  if (now - imuLast < 8) return;   // ~125 Hz
  imuLast = now;

  float ax, ay, az;
  if (!qmi.getAccelerometer(ax, ay, az)) return;

  float gx = 0, gy = 0, gz = 0;
  (void)qmi.getGyroscope(gx, gy, gz);

  const float accMag  = sqrtf(ax*ax + ay*ay + az*az);
  const float gyroMag = sqrtf(gx*gx + gy*gy + gz*gz);

  // DEBUG SEMPRE (rate limit)
  static unsigned long dbgT = 0;
  if (now - dbgT >= 150) {
    dbgT = now;
    USBSerial.printf("[IMU RAW] acc=%.2f gyro=%.1f ax=%.2f ay=%.2f az=%.2f\n",
                     accMag, gyroMag, ax, ay, az);
  }

  // ===================== WORRIED / TILT (INVARIATO) =====================
  const float TILT_ON_AY   = 0.80f;
  const float TILT_OFF_AY  = 0.90f;
  const float TILT_ON_AXZ  = 0.50f;
  const unsigned long NORMAL_DWELL_MS = 700;

  static bool          sTiltActive  = false;
  static unsigned long sNormalSince = 0;

  bool tilted_on = (ay <= TILT_ON_AY) && (fabsf(ax) >= TILT_ON_AXZ || az >= TILT_ON_AXZ);

  if (tilted_on && !sTiltActive && !gWorriedMode) {
    if (gMouthSeqActive) Faces_StopMouthSequence();

    const char* eyesArr[1]  = { wOpen  };
    const char* mouthArr[1] = { wMouth };

    Faces_BlinkSetAssetsRaw(
      eyesArr, 1, wClosed,
      mouthArr, 1, wMouth,
      /*drawFirstFrame=*/true,
      /*enableEyesBlink=*/true,
      /*enableMouthBlink=*/true,
      /*resetPhases=*/true
    );

    if (!Faces_CamIsBusy()) {
      (void)Faces_EyesLoadURL(wOpen);
      (void)Faces_MouthLoadURL(wMouth);
    }

    dfplayer.play(49);

    gWorriedMode  = true;
    gWorriedStart = now;
    sTiltActive   = true;
    sNormalSince  = 0;

    Faces_DeferWhoNow();
    return;
  }

  if (sTiltActive) {
    if (ay >= TILT_OFF_AY) {
      if (sNormalSince == 0) sNormalSince = now;

      if ((now - sNormalSince) >= NORMAL_DWELL_MS && !gMouthSeqActive) {
        gWorriedMode  = false;
        sTiltActive   = false;
        sNormalSince  = 0;

        FaceBlink::Enable(true, true);
        FaceBlink::ResetPhases(millis());

        Expressions_SetActive(ExprKind::Natural);
        Expressions_Play(ExprKind::Natural, 4);

        Faces_DeferWhoNow();
      }
    } else {
      sNormalSince = 0;
    }
  }

  // ===================== PICKED UP (LIFTED) + PUT DOWN =====================
  {
    static unsigned long cooldownUntil = 0;

    static unsigned long stillSince = 0;
    static bool          armed      = false;
    static unsigned long armUntil   = 0;

    // stato “in mano”
    static bool          inHand     = false;
    static unsigned long restSince  = 0;

    // Gate: se worried/tilt attivi, non fare Lifted né put-down
    if (gWorriedMode || sTiltActive) {
      stillSince = 0;
      armed = false;
      // NON resetto inHand qui: se lo stai tenendo e parte worried, non voglio rimbalzi
      return;
    }

    // Gate: non durante modalità "attive" (come volevi)
    const ExprKind k = Expressions_GetActive();
    const bool blocked =
        (k == ExprKind::Dance || k == ExprKind::Sing || k == ExprKind::Run || k == ExprKind::Avoid ||
         k == ExprKind::Fear  || k == ExprKind::Sleep || k == ExprKind::Yawn);
    if (blocked) {
      stillSince = 0;
      armed = false;
      return;
    }

    // Cooldown dopo trigger / after put-down
    if ((int32_t)(now - (int32_t)cooldownUntil) < 0) {
      stillSince = 0;
      armed = false;
      return;
    }

    // ---- (A) SE È IN MANO: cerco il “riappoggiato” e torno Natural ----
    if (inHand) {
      const bool restNow = (accMag > 0.98f && accMag < 1.12f) && (gyroMag < 3.0f);

      if (restNow) {
        if (restSince == 0) restSince = now;

        if ((now - restSince) >= 600) {
          USBSerial.println("[IMU] put down -> NATURAL");

          inHand = false;
          restSince = 0;

          // torna a Natural
          Expressions_SetActive(ExprKind::Natural);

          // opzionale: vocina “grazie”
           Expressions_PlayVariant(ExprKind::Natural, 4);

          Faces_DeferWhoNow();

          // piccolo cooldown per evitare retrigger mentre lo sistemi
          cooldownUntil = now + 1200;

          // reset macchina stati pickup
          armed = false;
          stillSince = 0;
          return;
        }
      } else {
        restSince = 0;
      }

      // mentre è in mano non faccio arming/trigger pickup
      return;
    }

    // ---- (B) NON è in mano: gestisco ARMING + TRIGGER pickup ----

    // STILL only for ARMING (anti-camminata)
    const bool stillNow = (accMag > 0.98f && accMag < 1.12f) && (gyroMag < 3.5f);

    // 1) ARMING
    if (!armed) {
      if (stillNow) {
        if (stillSince == 0) stillSince = now;
        if ((now - stillSince) >= 700) {
          armed = true;
          armUntil = now + 3000; // finestra comoda
          USBSerial.println("[IMU] armed for pickup");
        }
      } else {
        stillSince = 0;
      }
      return;
    }

    // 2) ARMED: aspetto evento entro finestra
    if ((int32_t)(now - (int32_t)armUntil) >= 0) {
      armed = false;
      stillSince = 0;
      return;
    }

    // Debug solo mentre armato
    static unsigned long armDbg = 0;
    if (now - armDbg >= 120) {
      armDbg = now;
      USBSerial.printf("[IMU ARM] acc=%.2f gyro=%.1f left=%ldms\n",
                       accMag, gyroMag, (long)((int32_t)armUntil - (int32_t)now));
    }

    // 3) TRIGGER pickup
    const float GYRO_PICK_THR = 7.0f;
    const float ACC_HIGH_THR  = 1.35f;
    const float ACC_LOW_THR   = 0.92f;

    const bool trig = (gyroMag >= GYRO_PICK_THR) || (accMag >= ACC_HIGH_THR) || (accMag <= ACC_LOW_THR);

    if (trig) {
      USBSerial.printf("[IMU] PICKUP -> LIFTED (acc=%.2f gyro=%.1f)\n", accMag, gyroMag);

      Expressions_NotifyUserActivity(now);
      Expressions_PlayVariant(ExprKind::Lifted, 1);
      Faces_DeferWhoNow();

      // passa a stato in mano (aspetta put-down)
      inHand = true;
      restSince = 0;

      // reset pickup + cooldown
      cooldownUntil = now + 3000;
      armed = false;
      stillSince = 0;
      return;
    }
  }
}




// =============================================================================
// WHO (face recognition)
// =============================================================================
static unsigned long s_lastWho = 0;
void Faces_DeferWhoNow() { s_lastWho = millis(); }

uint8_t PickRandomTrackFor(const String& who); // da espressioni.cpp/h

static uint32_t TrackLenMs(int track) {
  switch (track) {
    case 1  : return  3000;
    case 2  : return  1000;
    case 3  : return  81000;
    case 4  : return  94200;
    case 5  : return  2200;
    case 6  : return  1200;
    case 7  : return  2000;
    case 17 : return  2500;
    case 18 : return  3500;
    case 19 : return  2500;
    case 38 : return  2200;
    case 39 : return  1500;
    case 41 : return  1500;
    case 46 : return   500;
    case 47 : return  3000;
    case 49 : return  3500;
    case 50 : return  1000;
    case 51 : return   800;
    case 52 : return  1000;
    case 53 : return  2000;
    case 54 : return   800;
    case 55 : return   800;
    default : return  1500;
  }
}

void checkFaceRecognition(unsigned long now) {
  if (Enroll_IsActive_Display()) return;
  if (gMouthSeqActive) return;
  if (Expressions_GetActive() == ExprKind::Greeting) return;
  if (Faces_CamIsBusy()) return;
  if (gWorriedMode) return;

  static unsigned long s_lastPoll = 0;
  if ((long)(now - (long)s_lastPoll) < 350) return;
  s_lastPoll = now;

  enum class GState : uint8_t { Idle, Playing };
  static GState sState = GState::Idle;
  static unsigned long sSpeechUntil = 0;
  static unsigned long sBlockUntil  = 0;
  const  unsigned long WHO_BLOCK_MS = 6000;

  if (sState == GState::Playing) {
    if ((long)(now - (long)sSpeechUntil) >= 0 && !gMouthSeqActive) {
      Expressions_SetActive(ExprKind::Natural);
      s_lastPoll = now + 600;
      sState = GState::Idle;
    }
    return;
  }

  auto pollWhoBurst = []() -> String {
    String best = "none";
    for (int i=0; i<3; ++i) {
      if (Faces_CamIsBusy()) break;
      String w = "none";
      bool ok = camWho(w);
      if (ok) {
        w.trim(); w.toLowerCase();
        if (w.length() == 0) w = "none";
        if (w != "none") { best = w; break; }
      }
      delay(30);
    }
    return best;
  };

  static String prevWho = "none";
  String who = pollWhoBurst();
  if (who != prevWho) {
    USBSerial.printf(">>> CAM who: '%s'\n", who.c_str());
    prevWho = who;
  }

  if (who == "none") return;
  if ((long)(now - (long)sBlockUntil) < 0) return;

  String w = who;
  if (w != "marco" && w != "francesco" && w != "unknown") w = "unknown";

  uint8_t track = PickRandomTrackFor(w);
  Expressions_Play(ExprKind::Greeting, track);

  uint32_t ms = TrackLenMs(track);
  sSpeechUntil = now + (ms ? ms : 1500);

  sBlockUntil = now + WHO_BLOCK_MS;
  sState = GState::Playing;

  Faces_DeferWhoNow();
}

// =============================================================================
// Main update
// =============================================================================
void updateFaces(unsigned long now) {
  // blink sempre (bocca disabilitata se seq attiva)
  FaceBlink::Tick(now, gMouthSeqActive);

  // worried timebox
  if (gWorriedMode) {
    if ((int32_t)(now - (int32_t)gWorriedStart) > (int32_t)gWorriedDuration) {
      gWorriedMode = false;
      Faces_DeferWhoNow();
    }
    return;
  }

  // mouth sequence tick
  if (gMouthSeqActive && gMouthSeqCount > 0) {
    if (gMouthSeqNextTs == 0) gMouthSeqNextTs = now + gMouthSeqFrameMs;
    if ((int32_t)(now - (int32_t)gMouthSeqNextTs) >= 0) {
      Faces_ShowMouthFromBuffer(gMouthSeqCache[gMouthSeqIndex]);

      gMouthSeqIndex++;
      if (gMouthSeqIndex >= gMouthSeqCount) {
        gMouthSeqIndex = 0;
        if (gMouthSeqLoops > 0) {
          gMouthSeqLoops--;
          if (gMouthSeqLoops == 0) Faces_StopMouthSequence();
        }
      }
      gMouthSeqNextTs = now + gMouthSeqFrameMs;
    }
  }
}


