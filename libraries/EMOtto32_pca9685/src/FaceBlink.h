// FaceBlink.h
#pragma once
#include <Arduino.h>

namespace FaceBlink {

enum class BlinkPhase : uint8_t { OPEN, BLINK };

// Tempi + comportamento
struct BlinkParams {
  uint16_t openMinMs;
  uint16_t openMaxMs;
  uint16_t blinkMinMs;   // durata fase BLINK (chiuso/“blink”)
  uint16_t blinkMaxMs;
  uint8_t  stayPct;      // % resta sulla stessa variante open
  uint8_t  randPct;      // % sceglie variante random (resto = ordine)
  uint8_t  extra;        // riservato (0)
};

// Callback file-based (obbligatori): disegnano direttamente il path
using ShowFileCB = void (*)(const char* path);

void Init(const BlinkParams& eyes, const BlinkParams& mouth);

void SetFileCallbacks(ShowFileCB eyesOpen,
                      ShowFileCB eyesClosed,
                      ShowFileCB mouthOpen,
                      ShowFileCB mouthBlink);

// Varianti: N open (max 8) + 1 closed (occhi) / 1 blink (bocca)
void SetEyesVariants (const char* const eyesOpen[],  uint8_t nOpen,  const char* eyesClosed);
void SetMouthVariants(const char* const mouthOpen[], uint8_t nOpen,  const char* mouthBlink);

// Abilitazioni & lock
void Enable(bool eyesOn, bool mouthOn);
void LockEyes(bool on);
void LockMouth(bool on);

// Parametri runtime
void SetEyesParams (const BlinkParams& p);
void SetMouthParams(const BlinkParams& p);

// Timer helper

void ResetPhases(unsigned long now);

// Tick centrale (chiamalo una sola volta nel loop UI)
void Tick(unsigned long now, bool mouthSeqActive);

} // namespace FaceBlink

