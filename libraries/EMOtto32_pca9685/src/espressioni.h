// espressioni.h — API generica pulita
#pragma once
#include <Arduino.h>
#include <stdint.h>


// Identificatore espressione
enum class ExprKind : uint8_t { Natural = 0, Angry = 1, Fear = 2, Greeting = 3, Sadness = 4, Embarrassment = 5, Disgust = 6, Anxiety = 7, Boredom = 8, Sleep = 9, Wakeup = 10, Love = 11, Avoid = 12, Dance = 13, Sing = 14, Run = 15, Yawn = 16 };

// -------- Manager ad alto livello --------
// Internamente mantiene 1 ExprState per preset e uno "attivo" corrente.
// Implementazione in espressioni.cpp.

/// Inizializza i preset (carica assets, imposta blink e primo frame).
void Expressions_Init();

/// Aggiorna SOLO l’espressione attiva (tick del motore).
void Expressions_Update(unsigned long now);

/// Imposta quale preset è attivo immediatamente (senza parlato).
/// Esegue uno Stop() soft del precedente e mostra subito il primo frame del nuovo.
void Expressions_SetActive(ExprKind kind);

// Avvia il parlato sull’espressione indicata (diventa anche attiva).
/// frameMs/speechMs = 0 -> usa i default della variante/preset.
void Expressions_Play(ExprKind kind,
                      int16_t  track,
                      uint16_t frameMsOverride = 0,
                      uint16_t speechMsOverride = 0);
					  

/// Ferma il parlato (se presente) e ripristina l’idle coerente del preset indicato.
/// Se il preset fermato è quello attivo, resta visivo in idle; altrimenti solo stop logico.
void Expressions_Stop(ExprKind kind);

/// Ritorna quale preset è attualmente attivo.
ExprKind Expressions_GetActive();

uint8_t PickRandomTrackFor(const String& who);

/// Converte la variante nell’ID traccia corretto e chiama Expressions_Play(...).
void Expressions_PlayVariant(ExprKind kind, uint8_t variantIndex);

// Notifica attività utente (comando ricevuto,  ecc.)
void Expressions_NotifyUserActivity(unsigned long now);

// Gestione automatica idle → sbadiglio → sleep
void Expressions_CheckIdle(unsigned long now);

// Disegna overlay ZZZ (da chiamare dopo il rendering della faccia)
void Expressions_DrawZzzOverlay();


void Aura_BootBegin();
void Aura_BootTick(unsigned long now);
bool Aura_BootIsRunning();
