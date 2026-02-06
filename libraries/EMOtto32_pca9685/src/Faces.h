// Faces.h — API minimale, pulita e scalabile (NO TOUCH)
#ifndef FACES_H
#define FACES_H

#include <Arduino.h>
#include <lvgl.h>
#include <initializer_list>
#include "Arduino_GFX_Library.h"

// ===== Dimensioni asset (RGB565) =====
#define EYES_W  220
#define EYES_H  100
#define MOUTH_W 220
#define MOUTH_H 100

// ===== Oggetti LVGL utili fuori =====
extern lv_obj_t*  gEyesCanvas;
extern lv_obj_t*  gMouthCanvas;
extern uint16_t*  gEyesBuf;
extern uint16_t*  gMouthBuf;

// ===== Dipendenze forward =====
class DFRobotDFPlayerMini;

extern DFRobotDFPlayerMini dfplayer;
extern Arduino_GFX* gfx;

// ======================================================================
// 1) Inizializzazione display + loop LVGL/animazioni
// ======================================================================
void Faces_InitDisplayAndLVGL();
void Faces_LvglLoop();                 // chiamare spesso
void updateFaces(unsigned long now);   // tick blink + eventuale parlato
bool WaitCamReady(unsigned long timeoutMs = 1200);


// IMU
void handleIMUOnly(unsigned long now);
bool initIMU_WaveshareStyle();         // inizializza SOLO l’IMU (stile Waveshare)

// ======================================================================
// 2) Gestione asset / Blink engine unificato
// ======================================================================

// API "raw" per array + count (core)
bool Faces_BlinkSetAssetsRaw(
    const char* const* eyesOpen,  uint8_t nEyes,
    const char*        eyesClosed,
    const char* const* mouthOpen, uint8_t nMouth,
    const char*        mouthBlink,
    bool drawFirstFrame   = false,
    bool enableEyesBlink  = true,
    bool enableMouthBlink = true,
    bool resetPhases      = true);

// Wrapper comodo: template (array statici)
template<size_t NE, size_t NM>
inline bool Faces_BlinkSetAssets(
    const char* const (&eyesOpen)[NE], const char* eyesClosed,
    const char* const (&mouthOpen)[NM], const char* mouthBlink,
    bool drawFirstFrame   = false,
    bool enableEyesBlink  = true,
    bool enableMouthBlink = true,
    bool resetPhases      = true)
{
  return Faces_BlinkSetAssetsRaw(
    eyesOpen, (uint8_t)NE,
    eyesClosed,
    mouthOpen, (uint8_t)NM,
    mouthBlink,
    drawFirstFrame, enableEyesBlink, enableMouthBlink, resetPhases
  );
}

// Wrapper comodo: initializer_list (attenzione: usa buffer temporanei nel .cpp)
bool Faces_BlinkSetAssets(std::initializer_list<const char*> eyesOpen,
                          const char* eyesClosed,
                          std::initializer_list<const char*> mouthOpen,
                          const char* mouthBlink,
                          bool drawFirstFrame   = false,
                          bool enableEyesBlink  = true,
                          bool enableMouthBlink = true,
                          bool resetPhases      = true);

// Wrapper “C/ExprEngine”
bool Faces_BlinkSetAssets_C(const char* const eyesOpen[],  uint8_t nEyes,
                            const char*       eyesClosed,
                            const char* const mouthOpen[], uint8_t nMouth,
                            const char*       mouthBlink,
                            bool drawFirstFrame,
                            bool enableEyesBlink,
                            bool enableMouthBlink,
                            bool resetPhases);

// Enable/params blink (wrapper senza esporre BlinkParams)
void Faces_BlinkEnable(bool eyes, bool mouth);

void Faces_SetBlinkParamsEyes(uint16_t openMin, uint16_t openMax,
                              uint16_t blinkMin, uint16_t blinkMax,
                              uint8_t  stayPct, uint8_t  randPct,
                              uint8_t  doubleBlinkPct = 0);

void Faces_SetBlinkParamsMouth(uint16_t openMin, uint16_t openMax,
                               uint16_t blinkMin, uint16_t blinkMax,
                               uint8_t  stayPct, uint8_t  randPct,
                               uint8_t  doubleBlinkPct = 0);

// ======================================================================
// 3) Asset correnti (setter/getter) + loader diretti
// ======================================================================
void setEyesURLs (const char* urlOpen, const char* urlClosed);
void setMouthURL (const char* url);
void setMouthBlinkURL(const char* url);

void        Faces_GetCurrentEyesURLs(const char** outOpen, const char** outClosed);
const char* Faces_GetCurrentMouthURL();
const char* Faces_GetCurrentMouthBlinkURL();

// Loader diretti su canvas (blocking) — fallback/uso immediato
bool Faces_EyesLoadURL (const char* url);
bool Faces_MouthLoadURL(const char* url);

// ======================================================================
// 4) Sequenza “parlato” (bocca animata a frame)
// ======================================================================
bool Faces_PreloadMouthSequence(const char* urls[], int count);
void Faces_StartMouthSequence(uint16_t frameMs, int loops = -1);
void Faces_StopMouthSequence();
void Faces_ArmMouthSequenceAfterDelay(uint16_t frameMs, int loops, uint16_t delayMs);
void Faces_SyncUpdate(unsigned long now);

// Boot Talk (offset audio/bocca)
void Faces_BootTalkBlocking(uint8_t track, uint16_t frameMs,
                            uint8_t frameCount, uint16_t offsetMs,
                            uint32_t audioMs);

void Faces_BootTalkArm(uint8_t track, uint16_t frameMs,
                       uint8_t frameCount, uint16_t offsetMs,
                       uint32_t audioMs);

// ======================================================================
// 5) WHO e coordinamento
// ======================================================================
void checkFaceRecognition(unsigned long now);
void Faces_DeferWhoNow();   // debounce WHO

// ======================================================================
// 6) Preload/cache multi-varianti (implementate in faces_display.cpp)
// ======================================================================
bool Faces_PreloadEyesMulti (const char* const urlsOpen[], uint8_t nOpen, const char* urlClosed);
bool Faces_PreloadMouthMulti(const char* const urls[],     uint8_t n);
bool Faces_PreloadMouthBlink(const char* url);

bool Faces_ShowEyesOpenURL(const char* url);   // copia da cache variante "open"
bool Faces_ShowEyesClosedCached();             // copia da cache "closed"
bool Faces_ShowMouthOpenURL(const char* url);  // copia da cache variante "open"
bool Faces_ShowMouthBlinkCached();             // copia da cache "blink"

bool Faces_ShowMouthFromBuffer(const uint16_t* buf);

// Draw “veloci” (cache -> canvas)
bool Faces_ShowEyes(bool closed);
bool Faces_ShowMouth();

// ======================================================================
// 7) Busy-guard CAM (usato da preload/WHO)
// ======================================================================
void Faces_CamBusyBegin(void);
void Faces_CamBusyEnd(void);
bool Faces_CamIsBusy(void);

// ======================================================================
// 8) Overlay grafici (Zzz ecc.)
// ======================================================================
void Faces_DrawBinAt(const char* filename, int16_t x, int16_t y,
                     uint8_t w, uint8_t h);
void Faces_HideZCanvas();

#endif // FACES_H
