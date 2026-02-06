#include "FaceBlink.h"

// Facciamo sapere al motore se la CAM è occupata (definita in Faces.cpp)
extern bool Faces_CamIsBusy(void);

namespace FaceBlink {

static ShowFileCB sShowEyesOpenFile   = nullptr;
static ShowFileCB sShowEyesClosedFile = nullptr;
static ShowFileCB sShowMouthOpenFile  = nullptr;
static ShowFileCB sShowMouthBlinkFile = nullptr;

struct Channel {
  const char* open[8] = {nullptr};
  uint8_t     nOpen = 0;
  const char* closedOrBlink = nullptr; // occhi: closed, bocca: blink
  BlinkParams p{};
  BlinkPhase  phase = BlinkPhase::OPEN;
  uint8_t     order[8]{};
  uint8_t     orderLen = 0;
  uint8_t     pos = 0;
  uint8_t     cur = 0;
  uint32_t    nextTs = 0;
  bool        enabled = true;
  bool        locked  = false;
};

static Channel sEyes, sMouth;

static inline void shuffle(uint8_t* a, uint8_t n){
  for (int i = (int)n - 1; i > 0; --i){
    int j = random(i + 1);
    uint8_t t = a[i]; a[i] = a[j]; a[j] = t;
  }
}

static inline uint32_t rr(uint16_t a, uint16_t b){
  if (b <= a) return a;
  return (uint32_t)a + (uint32_t)random((uint32_t)(b - a + 1));
}

static void orderInit(Channel& c){
  c.orderLen = c.nOpen;
  for (uint8_t i=0;i<c.orderLen;i++) c.order[i]=i;
  if (c.orderLen > 1) shuffle(c.order, c.orderLen);
  c.pos = 0;
  c.cur = (c.orderLen ? c.order[0] : 0);
}

static inline void clampU16(uint16_t &v, uint16_t lo, uint16_t hi){
  if (v < lo) v = lo;
  else if (v > hi) v = hi;
}

static void sanitizeParams(){
  // Clamp “larghi” compatibili con i tuoi ExprAssets
  // Occhi
  clampU16(sEyes.p.blinkMinMs, 50,   800);
  clampU16(sEyes.p.blinkMaxMs, sEyes.p.blinkMinMs, 1200);
  clampU16(sEyes.p.openMinMs,  200,  60000);
  clampU16(sEyes.p.openMaxMs,  sEyes.p.openMinMs, 60000);

  // Bocca
  clampU16(sMouth.p.blinkMinMs, 50,   800);
  clampU16(sMouth.p.blinkMaxMs, sMouth.p.blinkMinMs, 1200);
  clampU16(sMouth.p.openMinMs,  200,  60000);
  clampU16(sMouth.p.openMaxMs,  sMouth.p.openMinMs, 60000);

  // Percentuali safe
  if (sEyes.p.stayPct > 100)  sEyes.p.stayPct = 100;
  if (sEyes.p.randPct > 100)  sEyes.p.randPct = 100;
  if ((uint16_t)sEyes.p.stayPct + (uint16_t)sEyes.p.randPct > 100) sEyes.p.randPct = 100 - sEyes.p.stayPct;

  if (sMouth.p.stayPct > 100) sMouth.p.stayPct = 100;
  if (sMouth.p.randPct > 100) sMouth.p.randPct = 100;
  if ((uint16_t)sMouth.p.stayPct + (uint16_t)sMouth.p.randPct > 100) sMouth.p.randPct = 100 - sMouth.p.stayPct;
}

void Init(const BlinkParams& eyes, const BlinkParams& mouth){
  sEyes.p = eyes;
  sMouth.p = mouth;
  sanitizeParams();

  sEyes.enabled = sMouth.enabled = true;
  sEyes.locked  = sMouth.locked  = false;

  sEyes.phase   = sMouth.phase   = BlinkPhase::OPEN;
  sEyes.nextTs  = sMouth.nextTs  = 0;
}

void SetFileCallbacks(ShowFileCB eyesOpen, ShowFileCB eyesClosed,
                      ShowFileCB mouthOpen, ShowFileCB mouthBlink){
  sShowEyesOpenFile   = eyesOpen;
  sShowEyesClosedFile = eyesClosed;
  sShowMouthOpenFile  = mouthOpen;
  sShowMouthBlinkFile = mouthBlink;
}

void SetEyesVariants(const char* const eyesOpen[], uint8_t n, const char* eyesClosed){
  sEyes.nOpen = (n>8?8:n);
  for (uint8_t i=0;i<sEyes.nOpen;i++) sEyes.open[i]=eyesOpen[i];
  for (uint8_t i=sEyes.nOpen;i<8;i++) sEyes.open[i]=nullptr;
  sEyes.closedOrBlink = eyesClosed;
  orderInit(sEyes);
}

void SetMouthVariants(const char* const mouthOpen[], uint8_t n, const char* mouthBlink){
  sMouth.nOpen = (n>8?8:n);
  for (uint8_t i=0;i<sMouth.nOpen;i++) sMouth.open[i]=mouthOpen[i];
  for (uint8_t i=sMouth.nOpen;i<8;i++) sMouth.open[i]=nullptr;
  sMouth.closedOrBlink = mouthBlink;
  orderInit(sMouth);
}

void Enable(bool eyesOn, bool mouthOn){ sEyes.enabled=eyesOn; sMouth.enabled=mouthOn; }

// Lock: quando sblocchi, riparti “morbido”
void LockEyes(bool on){
  sEyes.locked = on;
  if (!on) {
    unsigned long now = millis();
    sEyes.phase  = BlinkPhase::OPEN;
    sEyes.nextTs = now + rr(sEyes.p.openMinMs, sEyes.p.openMaxMs);
  }
}
void LockMouth(bool on){
  sMouth.locked = on;
  if (!on) {
    unsigned long now = millis();
    sMouth.phase  = BlinkPhase::OPEN;
    sMouth.nextTs = now + rr(sMouth.p.openMinMs, sMouth.p.openMaxMs);
  }
}

void SetEyesParams (const BlinkParams& p){ sEyes.p = p; sanitizeParams(); }
void SetMouthParams(const BlinkParams& p){ sMouth.p = p; sanitizeParams(); }

void ResetPhases(unsigned long now){
  sEyes.phase  = BlinkPhase::OPEN;
  sMouth.phase = BlinkPhase::OPEN;

  sEyes.nextTs  = now + rr(sEyes.p.openMinMs,  sEyes.p.openMaxMs);
  sMouth.nextTs = now + rr(sMouth.p.openMinMs, sMouth.p.openMaxMs);
}

static uint8_t pickNextIdx(Channel& c){
  if (c.nOpen <= 1) return 0;

  uint8_t nextIdx = c.cur;
  uint8_t dice = (uint8_t)random(100);

  if (dice < c.p.stayPct) {
    // stay
  } else if (dice < (uint8_t)(c.p.stayPct + c.p.randPct)) {
    do { nextIdx = (uint8_t)random(c.nOpen); } while(nextIdx==c.cur);
  } else if (c.orderLen > 0) {
    c.pos = (c.pos + 1) % c.orderLen;
    nextIdx = c.order[c.pos];
  }
  return nextIdx;
}

static inline void drawEyesOpen (){
  if (sShowEyesOpenFile && sEyes.nOpen && sEyes.open[sEyes.cur]) sShowEyesOpenFile(sEyes.open[sEyes.cur]);
}
static inline void drawEyesClosed(){
  if (sShowEyesClosedFile && sEyes.closedOrBlink) sShowEyesClosedFile(sEyes.closedOrBlink);
}
static inline void drawMouthOpen(){
  if (sShowMouthOpenFile && sMouth.nOpen && sMouth.open[sMouth.cur]) sShowMouthOpenFile(sMouth.open[sMouth.cur]);
}
static inline void drawMouthBlink(){
  if (sShowMouthBlinkFile && sMouth.closedOrBlink) sShowMouthBlinkFile(sMouth.closedOrBlink);
}

static void tickEyes(unsigned long now){
  if (!sEyes.enabled || sEyes.locked || sEyes.nOpen==0) return;
  if ((int32_t)(now - sEyes.nextTs) < 0) return;

  if (sEyes.phase == BlinkPhase::OPEN) {
    // open -> blink
    if (Faces_CamIsBusy()) { sEyes.nextTs = now + 40; return; }
    drawEyesClosed();
    sEyes.phase  = BlinkPhase::BLINK;
    sEyes.nextTs = now + rr(sEyes.p.blinkMinMs, sEyes.p.blinkMaxMs);
  } else {
    // blink -> open (next variant)
    sEyes.cur    = pickNextIdx(sEyes);
    drawEyesOpen();
    sEyes.phase  = BlinkPhase::OPEN;
    sEyes.nextTs = now + rr(sEyes.p.openMinMs, sEyes.p.openMaxMs);
  }
}

static void tickMouth(unsigned long now, bool mouthSeqActive){
  if (!sMouth.enabled || sMouth.locked) return;

  // Parlato: non fare scheduling “accumulato”, riparti pulito dopo
  if (mouthSeqActive) {
    sMouth.phase  = BlinkPhase::OPEN;
    sMouth.nextTs = now + rr(sMouth.p.openMinMs, sMouth.p.openMaxMs);
    return;
  }

  if ((int32_t)(now - sMouth.nextTs) < 0) return;

  if (sMouth.phase == BlinkPhase::OPEN) {
    if (Faces_CamIsBusy()) { sMouth.nextTs = now + 40; return; }
    drawMouthBlink();
    sMouth.phase  = BlinkPhase::BLINK;
    sMouth.nextTs = now + rr(sMouth.p.blinkMinMs, sMouth.p.blinkMaxMs);
  } else {
    sMouth.cur    = pickNextIdx(sMouth);
    drawMouthOpen();
    sMouth.phase  = BlinkPhase::OPEN;
    sMouth.nextTs = now + rr(sMouth.p.openMinMs, sMouth.p.openMaxMs);
  }
}

void Tick(unsigned long now, bool mouthSeqActive){
  tickEyes(now);
  tickMouth(now, mouthSeqActive);
}

} // namespace FaceBlink
