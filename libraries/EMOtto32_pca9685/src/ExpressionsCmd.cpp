// ExpressionsCmd.cpp — gestione centralizzata comandi serial / BLE
#include <Arduino.h>
#include "ExpressionsCmd.h"
#include "HWCDC.h"
#include "espressioni.h"     // Expressions_* e ExprKind
#include "wifiCam.h"

// Puntatore alla seriale / BLE
static HWCDC* gSerial = nullptr;

// --- gestione idle / sleep ---
static unsigned long gLastActivityMs = 0;
static bool gSleepMode = false;
static bool gYawnDone = false;

static const unsigned long IDLE_YAWN_MS  = 20000; // dopo 20s sbadiglio
static const unsigned long IDLE_SLEEP_MS = 26000; // dopo 26s dorme


// Inizializzazione: passare la seriale / BLE da usare
void ExpressionsCmd_Init(HWCDC* serial) {
  gSerial = serial;
  gLastActivityMs = millis();  // parte “attivo” ora
  gSleepMode = false;
  gYawnDone  = false;
}

void ExpressionsCmd_UpdateIdle(unsigned long now) {



  unsigned long idle = now - gLastActivityMs;

  if (!gSleepMode) {
    if (!gYawnDone && idle > IDLE_YAWN_MS && idle < IDLE_SLEEP_MS) {
      gYawnDone = true;
      Expressions_PlayVariant(ExprKind::Yawn, 1);
      if (gSerial) gSerial->println("[EXP] auto-YAWN");
    }

    if (idle >= IDLE_SLEEP_MS) {
      gSleepMode = true;
      Expressions_SetActive(ExprKind::Sleep);
      if (gSerial) gSerial->println("[EXP] auto-SLEEP");
    }
  }
}


// helper: estrae eventuale numero a fine comando, 0 se assente/non valido
static int parseIndex(const String& cmd) {
  int sp = cmd.lastIndexOf(' ');
  if (sp < 0) return 0;               // 0 => default/alternanza
  int v = cmd.substring(sp + 1).toInt();
  if (v < 1 || v > 9) return 0;       // margine alto, poi clampiamo per-kind
  return v;
}

// ====== mapping Greeting: blocchi di varianti ======
// Ordine in A_GREET (come nel tuo .cpp): 
//   [1..3] Unknown  | [4..6] Marco | [7..9] Francesco
static uint8_t pickGreetingVariantFor(const String& who) {
  static uint8_t u=1, m=4, f=7;   // round-robin per ciascun gruppo

  String w = who; w.toLowerCase();
  if (w == "marco") {
    uint8_t out = m; m++; if (m > 6) m = 4;
    return out;
  }
  if (w == "francesco") {
    uint8_t out = f; f++; if (f > 9) f = 7;
    return out;
  }
  // unknown / altri
  uint8_t out = u; u++; if (u > 3) u = 1;
  return out;
}

// Funzione da chiamare ogni loop per leggere comandi
void ExpressionsCmd_Poll() {
  if (!gSerial || !gSerial->available()) return;

    String cmd = gSerial->readStringUntil('\n');
  cmd.trim();
  if (cmd.length() == 0) return;

  unsigned long now = millis();
  Expressions_NotifyUserActivity(now);  // 👈 USA QUESTA
  
  // Log d'aiuto per capire *cosa* arriva davvero
  gSerial->printf("[CMD RAW] '%s' (len=%d)\n", cmd.c_str(), cmd.length());

  // ===== Alias CAM (se vuoi tenerli) =====
  if (cmd == "stream on")        cmd = "cam stream on";
  else if (cmd == "stream off")  cmd = "cam stream off";
  else if (cmd == "stream url")  cmd = "cam stream url";
  else if (cmd == "view on")     cmd = "cam view on";
  else if (cmd == "view off")    cmd = "cam view off";
  // lascia commentati gli alias enroll per evitare ambiguità:
  // else if (cmd == "enroll once") cmd = "cam enroll once";
  // else if (cmd == "enroll on")   cmd = "cam enroll on";
  // else if (cmd == "enroll off")  cmd = "cam enroll off";

  // ===== (A) COMANDI ESPRESSIONI =====

  // OFF → torna a Natural idle
  if (cmd == "ang off" || cmd == "arr off" ||
      cmd == "fear off"|| cmd == "pau off" ||
      cmd == "nat off"  ||
      cmd == "sad off" ||
      cmd == "tri off"
	  )
  {
    Expressions_SetActive(ExprKind::Natural);
    gSerial->println("[EXP] -> natural idle");
    return;
  }

  // --- NATURAL ---
  if (cmd.startsWith("nat")) {
    int idx = parseIndex(cmd);

    // alterna SOLO 2 <-> 3 quando non specifichi numero
    static uint8_t alt = 2;                 // parte da 2
    uint8_t var;
    if (idx == 0) {                         // "nat" senza numero
      var = alt;                            // 2 o 3
      alt = (alt == 2) ? 3 : 2;             // prepara alternanza
    } else {
      // consenti 1..4 esplicitamente
      if (idx >= 1 && idx <= 4) var = (uint8_t)idx;
      else var = alt;                       // numeri fuori range → continuiamo 2↔3
    }

    // QUI passiamo l’INDICE VARIANTE (1..4), non l’ID traccia
    Expressions_PlayVariant(ExprKind::Natural, var);
    gSerial->printf("[EXP] NAT var=%u\n", var);
    return;
  }

  // --- ANGRY (alias: "ang" e "arr") ---
  if (cmd.startsWith("ang") || cmd.startsWith("arr")) {
    int idx = parseIndex(cmd);

    // alterna 1 <-> 2 se non specifichi
    static uint8_t alt = 1;
    uint8_t var = (idx == 0) ? alt : (uint8_t)idx;
    if (idx == 0) alt = (alt == 1) ? 2 : 1;

    // clamp 1..3 (ANG_TALKS ha 3 varianti)
    if (var < 1) var = 1;
    if (var > 3) var = 3;

    Expressions_PlayVariant(ExprKind::Angry, var);
    gSerial->printf("[EXP] ANGRY var=%u\n", var);
    return;
  }

  // --- FEAR (alias: "fear" e "pau") ---
  if (cmd.startsWith("fear") || cmd.startsWith("pau")) {
    int idx = parseIndex(cmd);

    // alterna 1 <-> 2 se non specifichi
    static uint8_t alt = 1;
    uint8_t var = (idx == 0) ? alt : (uint8_t)idx;
    if (idx == 0) alt = (alt == 1) ? 2 : 1;

    // clamp 1..3 (FEAR_TALKS ha 3 varianti)
    if (var < 1) var = 1;
    if (var > 3) var = 3;

    Expressions_PlayVariant(ExprKind::Fear, var);
    gSerial->printf("[EXP] FEAR var=%u\n", var);
    return;
  }

// --- Sad (alias: "sad" e "tri") ---
  if (cmd.startsWith("sad") || cmd.startsWith("tri")) {
    int idx = parseIndex(cmd);

    // alterna 1 <-> 2 se non specifichi
    static uint8_t alt = 1;
    uint8_t var = (idx == 0) ? alt : (uint8_t)idx;
    if (idx == 0) alt = (alt == 1) ? 2 : 1;

    // clamp 1..3 (SAD_TALKS ha 3 varianti)
    if (var < 1) var = 1;
    if (var > 3) var = 3;

    Expressions_PlayVariant(ExprKind::Sadness, var);
    gSerial->printf("[EXP] SAD var=%u\n", var);
    return;
  }
  
  // --- GREETING ---
  if (cmd.startsWith("greet")) {
    String who = "unknown";
    int sp = cmd.lastIndexOf(' ');
    if (sp > 0) { who = cmd.substring(sp + 1); who.trim(); }

    uint8_t var = pickGreetingVariantFor(who);   // 1..9 mappato per nome
    Expressions_PlayVariant(ExprKind::Greeting, var);
    gSerial->printf("[EXP] GREET %s (var=%u)\n", who.c_str(), var);
    return;
  }

  // ===== (B) SOLO ORA: comandi CAM =====
  {
    String out;
    if (wifiCamHandleCmd(cmd, &out)) {
      if (out.length()) gSerial->println(out);
      return;
    }
  }

  gSerial->printf("[CMD] sconosciuto: %s\n", cmd.c_str());
}
