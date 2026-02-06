#ifndef BLUETOOTH_H
#define BLUETOOTH_H

#include <Arduino.h>

// Avvia BLE in advertising (periferica pronta alla connessione)
void initBLE();

// Tenuto per compatibilità: non fa nulla, ma puoi lasciarlo nel loop()
void loopBLE();

// Stato connessione attuale
bool bleIsConnected();

// === EMOtto 3.0: movimento continuo controllato via app ===
// Da chiamare nel loop principale: Ble_UpdateWalking(millis());
void Ble_UpdateWalking(unsigned long now);

// Getter utili (opzionali)
int Ble_GetSpeedMs();
int Ble_GetVolume();

#endif
