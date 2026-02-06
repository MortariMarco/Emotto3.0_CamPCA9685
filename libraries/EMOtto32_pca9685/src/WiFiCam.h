//WIFICam.h
#ifndef WIFICAM_H
#define WIFICAM_H

#include <Arduino.h>
#include <WiFi.h>

// Connessione STA all'AP della CAM (SSID/PSK)
bool initWiFiCam(const char* ssid, const char* password);

// Stato Wi-Fi (per StatusBar ecc.)
bool wifiIsConnected();

// Avvio link UART verso CAM
// Esempio sul display: wifiCamBeginUart(/*rx=*/3, /*tx=*/2, /*baud=*/2000000, /*uartNum=*/1);
void wifiCamBeginUart(int rx, int tx, uint32_t baud = 2000000, int uartNum = 1);

// Scarica esattamente 'len' byte dal file .bin indicato (solo nomi/percorsi .bin)
// Ritorna true se sono stati ricevuti tutti i 'len' byte e il footer "DONE"
bool camFetchExact(const char* nameOrPath, uint8_t* dst, size_t len);

// Ottiene il WHO via UART ("WHO\n" -> "marco\n" / "none\n")
bool camWho(String& outName);

void wifiCamPump(const char* ssid);



// ---- Controllo streaming MJPEG (via UART CAM) ----
bool camStreamOn();          // avvia il server MJPEG sulla CAM
bool camStreamOff();         // ferma lo stream (se supportato)
bool camStreamGetUrl(String &outUrl);  // chiede l'URL e lo ritorna

// --- viewer snapshot ---
bool camViewStart();      // avvia polling /jpeg
void camViewStop();       // ferma viewer
void camViewTick(uint32_t now);      // chiamalo spesso nel 
bool camViewIsOn();
void camViewSetRotation(uint8_t rot90);   // 0,1,2,3 (= 0/90/180/270°)

// ---- Controllo enroll (via UART CAM) ----
bool camEnrollOnce();        // scatta/enroll una volta alla prossima faccia
bool camEnrollOn();          // modalità enroll continua
bool camEnrollOff();         // disattiva enroll

bool wifiCamHandleCmd(const String &cmd, String *outMsg = nullptr);

bool camEnrollSetName(const char* name);   // invia "ENROLL NAME <name>"

// --- Enroll (display side) ---
bool Enroll_On_Display();
void Enroll_Off_Display();
void Enroll_Tick_Display(uint32_t nowMs);
void Enroll_SaveYes_Display();
void Enroll_SaveNo_Display();
void Enroll_SendName_Display(const char* name);
bool Enroll_IsActive_Display();

#endif
