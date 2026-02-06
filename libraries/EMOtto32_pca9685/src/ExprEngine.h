// ExprEngine.h
#pragma once
#include <stdint.h>
#include <stddef.h>

struct TalkVariant {
  int16_t            track;            // traccia DFPlayer, -1 = nessun audio
  const char* const* frames;           // sequenza bocca (A.bin, O.bin, ...)
  uint8_t            count;            // quante frame nella sequenza
  uint16_t           frameMsDefault;   // durata frame default
  uint16_t           speechMsDefault;  // durata parlato default per calcolo loops
};

struct ExprAssets {
  const char* const* eyesOpen;  uint8_t nEyesOpen;
  const char*        eyesClosed;

  const char* const* mouthOpen; uint8_t nMouthOpen;
  const char*        mouthBlink;

  const TalkVariant* talks;     uint8_t nTalks;

  // --- Occhi ---
  uint16_t eyesOpenMin,  eyesOpenMax;
  uint16_t eyesBlinkMin, eyesBlinkMax;
  uint8_t  eyesStayPct,  eyesRandPct;
  uint8_t  eyesDoubleBlinkPct;

  // --- Bocca ---
  uint16_t mouthOpenMin,  mouthOpenMax;
  uint16_t mouthBlinkMin, mouthBlinkMax;
  uint8_t  mouthStayPct,  mouthRandPct;
  uint8_t  mouthDoubleBlinkPct;

  int16_t speechOffsetMs;
};


struct ExprState {
  enum Mode : uint8_t { Idle, Talk, IdleLoop } mode = Idle;

  // parlato/audio
  bool     soundPending   = false;
  int16_t  pendingTrack   = -1;
  uint32_t soundStartAt   = 0;
  uint32_t phaseUntil     = 0;       // fine sequenza parlato

  // selezione talk & tempi effettivi
  uint8_t  talkIdx        = 0;
  uint16_t frameMsEff     = 150;
  uint16_t speechMsEff    = 2000;

  // varianti random
  uint8_t  eyeOrder[8]    = {0};  uint8_t eyeOrderLen=0, eyePos=0, eyeCurIdx=0;
  uint8_t  mouthOrder[8]  = {0};  uint8_t mouthOrderLen=0, mouthPos=0, mouthCurIdx=0;
  uint32_t eyeNextTs=0, mouthNextTs=0;

  // per Stop/Play pre-emption
  bool     activeAssetsSet = false;
};

/// API generica
void Expr_Init  (const ExprAssets& A, ExprState& S);
// if (A.nEyesOpen  > 8) /* clamp a 8 */;
// if (A.nMouthOpen > 8) /* clamp a 8 */;
void Expr_Play  (const ExprAssets& A, ExprState& S,
                 int16_t track, uint16_t frameMsOverride=0, uint16_t speechMsOverride=0);
void Expr_Update(const ExprAssets& A, ExprState& S, unsigned long now);
void Expr_Stop  (const ExprAssets& A, ExprState& S);

// helpers (implementati in .cpp)
uint16_t Expr_LoopsFromDuration(uint16_t frameMs, uint8_t frameCount, uint16_t speechMs);

extern bool gForcePreloadTalkFrames;
