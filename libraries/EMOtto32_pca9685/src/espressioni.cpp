// espressioni.cpp — manager semplice sopra ExprEngine
#include "espressioni.h"
#include "ExprEngine.h"
#include "Faces.h"
#include "FaceBlink.h"
#include "HWCDC.h"
#include <esp_system.h>  // per esp_random()
#include "aura_ws2812.h"
#include "ttp223.h"
#include <EMOtto.h>

extern Otto Otto; 

static inline void TtpBeepTap() {
  Otto.sing(S_happy);
}

static inline void TtpBeepDouble() {
  Otto.sing(S_superHappy);   // se non esiste, lo cambiamo
}

static inline void TtpBeepLong() {
  Otto.sing(S_sleeping);     // oppure S_confused / S_fart3
}

// ttp touch
#define TTP_PIN 16
static TTP223 gTtp(TTP_PIN, true);
static bool gTtpInit = false;

static inline void TtpEnsureInit() {
  if (!gTtpInit) {
    gTtp.begin(false);   // false = niente pull (di solito TTP223 ha già il suo)
    gTtpInit = true;
  }
}
// neopixel
#ifndef WS_PIN
  #define WS_PIN 43   // il tuo U0TXD che hai verificato funzionare
#endif
#ifndef WS_LEDS
  #define WS_LEDS 8  // numero LED
#endif

extern HWCDC USBSerial;

static void Expr_ApplyAssetsNow(const ExprAssets& A, ExprState& S);

static void Lifted_MoveLegs() {
  // movimento corto: 200–600ms totale (non bloccare troppo)
  // scegli UNA delle seguenti, dipende da cosa supporta la tua libreria Otto:

  // Opzione A: “shake leg” (molto effetto panico)
  Otto.shakeLeg(1, 500, 1);   // (steps, T, dir)  dir 1 o -1

  // Opzione B: mini “bend” (si abbassa e risale)
   Otto.bend(1, 450, 1);

  // Opzione C: un micro passo avanti-indietro
  // Otto.walk(1, 450, 1);
  // Otto.walk(1, 450, -1);
}

//neopixel
 static AuraWS2812 gAura(WS_PIN, WS_LEDS);
static bool gAuraInit = false;

static inline void AuraEnsureInit() {
  if (!gAuraInit) {
    gAura.begin(80);      // brightness iniziale (0..255)
    gAuraInit = true;
  }
}

static bool sAuraBootRunning = false;
static unsigned long sAuraBootT0 = 0;
static const unsigned long AURA_BOOT_MS = 3000;

void Aura_BootBegin() {
  AuraEnsureInit();
  gAura.setBrightness(180);
  sAuraBootRunning = true;
  sAuraBootT0 = millis();
}

void Aura_BootTick(unsigned long now) {
  if (!sAuraBootRunning) return;

  // effetto "loading"
  gAura.update(now, ExprKind::Wakeup);

  // tieni vivo UI/bocca anche durante boot
  Faces_LvglLoop();
  updateFaces(now);
  Faces_SyncUpdate(now);

  if ((uint32_t)(now - sAuraBootT0) >= (uint32_t)AURA_BOOT_MS) {
    sAuraBootRunning = false;
    gAura.setBrightness(80);
  }
}

bool Aura_BootIsRunning() {
  return sAuraBootRunning;
}




// --------- ASSET NATURAL ---------
static const char* NAT_EYES[]  = { "occhi_aperti.bin","occhi_dx.bin","occhi_sx.bin","occhi_basso.bin" };
static const char* NAT_MOUTH[] = { "bocca_sorride.bin","bocca_sorride2.bin","dritta.bin","bocca_sorride.bin" };

// --------- NATURAL (3 voci) ---------
static const char* TALK_NAT_V1[] = {"C.bin","I.bin","A.bin","O.bin"};                 // 4 frame
static const char* TALK_NAT_V2[] = {"A.bin","E.bin","I.bin","O.bin","U.bin"};         // 5 frame
static const char* TALK_NAT_V3[] = {"A.bin","O.bin"};                                  // 2 frame, più “lenta”
static const char* TALK_NAT_V4[] = {"E.bin","A.bin","O.bin","I.bin"};  

static const TalkVariant NAT_TALKS[] = {
  {  1, TALK_NAT_V1, (uint8_t)(sizeof(TALK_NAT_V1)/sizeof(TALK_NAT_V1[0])), 150, 1800 },//ciao_mi chiamo emotto.
  {  53, TALK_NAT_V2, (uint8_t)(sizeof(TALK_NAT_V2)/sizeof(TALK_NAT_V2[0])), 120, 1600 },//SonoFelice
  {  54, TALK_NAT_V3, (uint8_t)(sizeof(TALK_NAT_V3)/sizeof(TALK_NAT_V3[0])), 180, 1400 },//Ehila.
  {  52, TALK_NAT_V4, (uint8_t)(sizeof(TALK_NAT_V4)/sizeof(TALK_NAT_V4[0])), 150, 1600 },//Grazie
};



static const ExprAssets A_NAT = {
  NAT_EYES, (uint8_t)(sizeof(NAT_EYES)/sizeof(NAT_EYES[0])), "occhi_chiusi.bin",
  NAT_MOUTH,(uint8_t)(sizeof(NAT_MOUTH)/sizeof(NAT_MOUTH[0])), "B_sorride_chiusa.bin",
  NAT_TALKS,(uint8_t)(sizeof(NAT_TALKS)/sizeof(NAT_TALKS[0])),
  // --- OCCHI (esempio) ---
  4000, 5000,   // eyesOpenMin,  eyesOpenMax
  150,  200,    // eyesBlinkMin, eyesBlinkMax
  15,   45,     // eyesStayPct,  eyesRandPct
  0,            // eyesDoubleBlinkPct

  // --- BOCCA (qui regoli il “blink” della bocca in idle) ---
  3000, 6000,   // mouthOpenMin,  mouthOpenMax   -> meno/ più frequente
  130,  180,    // mouthBlinkMin, mouthBlinkMax  -> più/meno veloce la chiusura
  10,   40,     // mouthStayPct,  mouthRandPct   -> “resta” / “random” variante
  0,            // mouthDoubleBlinkPct           -> colpetto doppio (%)

  0          // speechOffsetMs (sync audio/bocca)
};



// --------- ASSET ANGRY ---------
static const char* ANG_EYES[]  = { "occhi_arrabbiati.bin","occhi_incazzatidx.bin","occhi_incazzatisx.bin","occhi_arrabbiati.bin" };
static const char* ANG_MOUTH[] = { "bocca_arrabbiata.bin","incazzata2sx.bin","digrigna.bin","incazzata2dx.bin" };

// --------- ANGRY (3 voci) ---------
static const char* TALK_ANG_V1[] = {"S.bin","O.bin","N.bin","R.bin","B.bin","O.bin"}; // 6 frame
static const char* TALK_ANG_V2[] = {"A.bin","R.bin","R.bin"};                          // 3 frame, più secca
static const char* TALK_ANG_V3[] = {"A.bin","R.bin","R.bin","O.bin"};                  // 4 frame, “digrigna”

static const TalkVariant ANG_TALKS[] = {
  { 17, TALK_ANG_V1, (uint8_t)(sizeof(TALK_ANG_V1)/sizeof(TALK_ANG_V1[0])), 140, 1800 },//SonoArrabbiato.
  { 18, TALK_ANG_V2, (uint8_t)(sizeof(TALK_ANG_V2)/sizeof(TALK_ANG_V2[0])), 110, 1200 },//Sonoarrabbiatocomeunpuma.
  { 50, TALK_ANG_V3, (uint8_t)(sizeof(TALK_ANG_V3)/sizeof(TALK_ANG_V3[0])), 120, 1500 },//Arghhhh
};
static const ExprAssets A_ANG = {
  ANG_EYES, (uint8_t)(sizeof(ANG_EYES)/sizeof(ANG_EYES[0])), "occhi_arrabbiati2.bin",
  ANG_MOUTH,(uint8_t)(sizeof(ANG_MOUTH)/sizeof(ANG_MOUTH[0])), "bocca_triste.bin",
  ANG_TALKS,(uint8_t)(sizeof(ANG_TALKS)/sizeof(ANG_TALKS[0])),
  // --- OCCHI (esempio) ---
  4000, 5000,   // eyesOpenMin,  eyesOpenMax
  150,  200,    // eyesBlinkMin, eyesBlinkMax
  15,   45,     // eyesStayPct,  eyesRandPct
  0,            // eyesDoubleBlinkPct

  // --- BOCCA (qui regoli il “blink” della bocca in idle) ---
  3000, 6000,   // mouthOpenMin,  mouthOpenMax   -> meno/ più frequente
  130,  180,    // mouthBlinkMin, mouthBlinkMax  -> più/meno veloce la chiusura
  10,   40,     // mouthStayPct,  mouthRandPct   -> “resta” / “random” variante
  0,            // mouthDoubleBlinkPct           -> colpetto doppio (%)

  +800          // speechOffsetMs (sync audio/bocca)
};



// --------- ASSET FEAR ---------
static const char* FEAR_EYES[]  = { "occhi_paura.bin","occhi_sx.bin","occhi_dx.bin","occhi_paura.bin" };
static const char* FEAR_MOUTH[] = { "b_paura.bin","digrigna.bin","bocca-guardadxsx.bin" };

// --------- FEAR (3 voci) ---------
static const char* TALK_FEAR_V1[] = {"A.bin","I.bin","O.bin"};                         // 3 frame
static const char* TALK_FEAR_V2[] = {"E.bin","I.bin","E.bin","O.bin"};                 // 4 frame
static const char* TALK_FEAR_V3[] = {"O.bin"};                                         // 1 frame, tremolio lento

static const TalkVariant FEAR_TALKS[] = {
  {  7, TALK_FEAR_V1, (uint8_t)(sizeof(TALK_FEAR_V1)/sizeof(TALK_FEAR_V1[0])), 150, 2000 },//_ChepauraAiuto
  {  55, TALK_FEAR_V2, (uint8_t)(sizeof(TALK_FEAR_V2)/sizeof(TALK_FEAR_V2[0])), 130, 1600 },//Youuuuu.
  {  50, TALK_FEAR_V3, (uint8_t)(sizeof(TALK_FEAR_V3)/sizeof(TALK_FEAR_V3[0])), 200, 1400 },//Arghhhh
};

static const ExprAssets A_FEAR = {
  FEAR_EYES, (uint8_t)(sizeof(FEAR_EYES)/sizeof(FEAR_EYES[0])), "occhi_chiusi.bin",
  FEAR_MOUTH,(uint8_t)(sizeof(FEAR_MOUTH)/sizeof(FEAR_MOUTH[0])), "bocca_triste.bin",
  FEAR_TALKS,(uint8_t)(sizeof(FEAR_TALKS)/sizeof(FEAR_TALKS[0])),
 // --- OCCHI (esempio) ---
  4000, 5000,   // eyesOpenMin,  eyesOpenMax
  150,  200,    // eyesBlinkMin, eyesBlinkMax
  15,   45,     // eyesStayPct,  eyesRandPct
  0,            // eyesDoubleBlinkPct

  // --- BOCCA (qui regoli il “blink” della bocca in idle) ---
  3000, 6000,   // mouthOpenMin,  mouthOpenMax   -> meno/ più frequente
  130,  180,    // mouthBlinkMin, mouthBlinkMax  -> più/meno veloce la chiusura
  10,   40,     // mouthStayPct,  mouthRandPct   -> “resta” / “random” variante
  0,            // mouthDoubleBlinkPct           -> colpetto doppio (%)

  +800          // speechOffsetMs (sync audio/bocca)
};

// --------- ASSET GREETING (RICONOSCIMENTO FACCIALE) ---------
// ===== GREETING: sequenze bocca (metti gli ID traccia reali del DFPlayer) =====

// UNKNOWN / ALTRO (es. 46, 53, 54)
static const char* TALK_UNK_A[] = {"C.bin","I.bin","A.bin","O.bin"};               // 4
static const char* TALK_UNK_B[] = {"A.bin","E.bin","I.bin","O.bin","E.bin"};       // 5
static const char* TALK_UNK_C[] = {"A.bin","O.bin"};                                // 2

// MARCO (es. 39, 51, 52)
static const char* TALK_MARCO_A[] = {"C.bin","I.bin","A.bin","O.bin","R.bin"};     // 5
static const char* TALK_MARCO_B[] = {"A.bin","I.bin","A.bin","O.bin"};             // 4
static const char* TALK_MARCO_C[] = {"C.bin","A.bin","O.bin"};                      // 3

// FRANCESCO (es. 38, 55, 56)
static const char* TALK_FRAN_A[]  = {"C.bin","I.bin","A.bin","O.bin","B.bin","R.bin","A.bin","N.bin"}; // 8
static const char* TALK_FRAN_B[]  = {"A.bin","E.bin","I.bin","O.bin"};              // 4
static const char* TALK_FRAN_C[]  = {"I.bin","O.bin","A.bin"};                      // 3

static const TalkVariant GREET_TALKS[] = {
  // Unknown/altro
  { 46, TALK_UNK_A,   (uint8_t)4, 150, 1800 },
  { 47, TALK_UNK_B,   (uint8_t)5, 130, 1700 },
  { 54, TALK_UNK_C,   (uint8_t)2, 180, 1400 },
  // Marco
  { 39, TALK_MARCO_A, (uint8_t)5, 150, 1600 },
  { 51, TALK_MARCO_B, (uint8_t)4, 140, 1500 },
  { 54, TALK_MARCO_C, (uint8_t)3, 160, 1500 },
  // Francesco
  { 38, TALK_FRAN_A,  (uint8_t)8, 150, 2000 },
  { 55, TALK_FRAN_B,  (uint8_t)4, 140, 1600 },
  { 41, TALK_FRAN_C,  (uint8_t)3, 160, 1500 },
};

static const char* GREET_EYES[]  = {"occhi_allegri.bin"};
static const char* GREET_MOUTH[] = {"bocca_sorride.bin"};

static const ExprAssets A_GREET = {
  GREET_EYES, 1, "occhi_chiusi.bin",
  GREET_MOUTH,1, "B_sorride_chiusa.bin",
  GREET_TALKS, (uint8_t)(sizeof(GREET_TALKS)/sizeof(GREET_TALKS[0])),

  // occhi
  3000, 4500, 120, 180, 20, 40, 0,
  // bocca (idle quasi assente: parla guida la bocca)
  5000, 7000, 160, 220, 5, 15, 0,

  +800
};

// --------- ASSET Sadness ---------
static const char* SAD_EYES[]  = { "occhi_piange.bin","occhi_piange2.bin","occhi_piange.bin","occhi_piange3.bin" };
static const char* SAD_MOUTH[] = { "bocca_piange.bin","bocca_piange2.bin","bocca_piange.bin","bocca-piange3.bin" };

// --------- Sadness (3 voci) ---------
static const char* TALK_SAD_V1[] = {"S.bin","O.bin","N.bin","T.bin","R.bin","O.bin"}; // 6 frame
static const char* TALK_SAD_V2[] = {"T.bin","R.bin","I.bin"};                          // 3 frame, più secca
static const char* TALK_SAD_V3[] = {"S.bin","T.bin","R.bin","E.bin"};                  // 4 frame, “digrigna”

static const TalkVariant SAD_TALKS[] = {
  { 19, TALK_SAD_V1, (uint8_t)(sizeof(TALK_SAD_V1)/sizeof(TALK_SAD_V1[0])), 140, 1800 },//SonoTristeuffa
  
  { 56, TALK_SAD_V2, (uint8_t)(sizeof(TALK_SAD_V2)/sizeof(TALK_SAD_V2[0])), 110, 1200 },//oggisonotriste.
  { 58, TALK_SAD_V3, (uint8_t)(sizeof(TALK_SAD_V3)/sizeof(TALK_SAD_V3[0])), 120, 1500 },//mivienedapiangere
};
static const ExprAssets A_SAD = {
  SAD_EYES, (uint8_t)(sizeof(SAD_EYES)/sizeof(SAD_EYES[0])), "occhi_piange.bin",
  SAD_MOUTH,(uint8_t)(sizeof(SAD_MOUTH)/sizeof(SAD_MOUTH[0])), "bocca_piange.bin",
  SAD_TALKS,(uint8_t)(sizeof(SAD_TALKS)/sizeof(SAD_TALKS[0])),
  // --- OCCHI (esempio) ---
  4000, 5000,   // eyesOpenMin,  eyesOpenMax
  150,  200,    // eyesBlinkMin, eyesBlinkMax
  15,   45,     // eyesStayPct,  eyesRandPct
  0,            // eyesDoubleBlinkPct

  // --- BOCCA (qui regoli il “blink” della bocca in idle) ---
  4000, 5000,   // mouthOpenMin,  mouthOpenMax   -> meno/ più frequente
  150,  200,    // mouthBlinkMin, mouthBlinkMax  -> più/meno veloce la chiusura
  15,   45,     // mouthStayPct,  mouthRandPct   -> “resta” / “random” variante
  0,            // mouthDoubleBlinkPct           -> colpetto doppio (%)

  +800          // speechOffsetMs (sync audio/bocca)
};

// --------- ASSET Yawn (Sbadiglio) ---------
// ATTENZIONE: sostituisci i nomi dei .bin con quelli REALI che hai su SD

static const char* YAWN_EYES[]  = {
  "occhi_sbadiglia1.bin",   // occhi che iniziano a chiudersi
  "occhi_sbadiglia2.bin",   // più chiusi
  "occhi_dorme.bin",   // quasi chiusi
  "occhi_sbadiglia1.bin"    // ritorno leggero
};

static const char* YAWN_MOUTH[] = {
  "O.bin",   // bocca che inizia ad aprirsi
  "incazzata2sx.bin",   // bocca molto aperta
  "bocca-badiglia3.bin",   // massimo sbadiglio
  "dritta.bin"    // ritorno
};

// --------- Yawn (1 voce – sbadiglio) ---------
// Track ID di esempio: 70  => cambialo con il numero reale sul DFPlayer
static const char* TALK_YAWN_V1[] = {
  "A.bin","O.bin","C.bin","I.bin"   // sequenza bocca “sbadiglio”
};

static const TalkVariant YAWN_TALKS[] = {
  // { trackId, seq[],           seqLen,                              frameMs, totalMs }
  { 2,        TALK_YAWN_V1, (uint8_t)(sizeof(TALK_YAWN_V1)/sizeof(TALK_YAWN_V1[0])),
               140,          1800 }  // ~1,8s di sbadiglio
};

static const ExprAssets A_YAWN = {
  // --- EYES ---
  YAWN_EYES,
  (uint8_t)(sizeof(YAWN_EYES)/sizeof(YAWN_EYES[0])),
  "occhi_sbadiglia1.bin",   // frame "base" per idle

  // --- MOUTH ---
  YAWN_MOUTH,
  (uint8_t)(sizeof(YAWN_MOUTH)/sizeof(YAWN_MOUTH[0])),
  "bocca-badiglia3.bin",   // frame "base" per idle

  // --- TALKS ---
  YAWN_TALKS,
  (uint8_t)(sizeof(YAWN_TALKS)/sizeof(YAWN_TALKS[0])),

  // --- BLINK OCCHI in idle (qui in realtà Yawn verrà usata poco in idle) ---
  2500, 3000,   // eyesOpenMin,  eyesOpenMax   -> apre/chiude ogni 2,5–3 s
  160,  220,    // eyesBlinkMin, eyesBlinkMax  -> velocità chiusura
  10,   30,     // eyesStayPct,  eyesRandPct
  0,            // eyesDoubleBlinkPct (0 = niente doppio)

  // --- BOCCA “blink” in idle (poco rilevante, la userai per la voce) ---
  3000, 4000,   // mouthOpenMin,  mouthOpenMax
  160,  220,    // mouthBlinkMin, mouthBlinkMax
  10,   30,     // mouthStayPct,  mouthRandPct
  0,            // mouthDoubleBlinkPct

  +600          // speechOffsetMs (leggera anticipazione/delay su audio)
};


//------addormentamento

// ====== ZZZ OVERLAY (bitmap, 3 dimensioni) ======

struct ZFrameInfo {
  const char* file;
  uint8_t     w;
  uint8_t     h;
};

// File .bin delle Z (da mettere su SD)
static const ZFrameInfo ZZZ_FRAMES[] = {
  { "Z_small.bin", 20, 20 },  // frame 0
  { "Z_mid.bin",   28, 28 },  // frame 1
  { "Z_big.bin",   36, 36 }   // frame 2
};
static const uint8_t ZZZ_N_FRAMES = (uint8_t)(sizeof(ZZZ_FRAMES)/sizeof(ZZZ_FRAMES[0]));

static bool         sZzzVisible   = false;
static int16_t      sZzzX         = 200;  // coord base (x) — adatta al tuo display
static int16_t      sZzzY         = 140;  // coord base (y) — vicino alla bocca
static uint8_t      sZzzFrame     = 0;
static unsigned long sZzzLastStep = 0;

// dichiarazione funzioni di drawing (faces_display.cpp)
extern void Faces_DrawBinAt(const char* filename, int16_t x, int16_t y,
                            uint8_t w, uint8_t h);
extern void Faces_HideZCanvas();

// ====== IDLE / YAWN / SLEEP ======
static unsigned long sLastActivityMs = 0;
static bool sSleepMode = false;
static bool sYawnDone  = false;

static const unsigned long IDLE_YAWN_MS  = 300000; // dopo 5 minuti sbadiglia
static const unsigned long IDLE_SLEEP_MS = 310000; // dopo poco piu  5 min  dorme

// Da chiamare quando c'è attività utente (comando,  ecc.)
void Expressions_NotifyUserActivity(unsigned long now) {
  sLastActivityMs = now;

  if (sSleepMode) {
    // era addormentato → sveglia
    sSleepMode  = false;
    sYawnDone   = false;

    // stop Zzz subito
    sZzzVisible = false;
    sZzzY       = 140;      // reset posizione opzionale
    Faces_HideZCanvas();    // nasconde subito il canvas Z

    // espressione di risveglio + "buongiorno"
    Expressions_PlayVariant(ExprKind::Wakeup, 1);
  }
}


void Expressions_DrawZzzOverlay() {
  if (!sZzzVisible) {
    Faces_HideZCanvas();
    return;
  }
  if (sZzzFrame >= ZZZ_N_FRAMES) return;

  const ZFrameInfo &f = ZZZ_FRAMES[sZzzFrame];
  Faces_DrawBinAt(f.file, sZzzX, sZzzY, f.w, f.h);
}

// Da chiamare nel loop(), dopo Expressions_Update(now)
void Expressions_CheckIdle(unsigned long now) {
  unsigned long idle = now - sLastActivityMs;

  if (!sSleepMode) {
    // Fase sbadiglio
    if (!sYawnDone && idle > IDLE_YAWN_MS && idle < IDLE_SLEEP_MS) {
      sYawnDone = true;
      Expressions_PlayVariant(ExprKind::Yawn, 1);  // sbadiglio
    }

    // Fase sleep
    if (idle >= IDLE_SLEEP_MS) {
      sSleepMode  = true;
      sZzzVisible = true;
      sZzzFrame   = 0;
      sZzzX       = 200;  // coord di partenza (puoi regolare)
      sZzzY       = 140;  // coord di partenza (vicino alla bocca)
      sZzzLastStep = now;

      // animazione / audio di sonno
      Expressions_PlayVariant(ExprKind::Sleep, 1); // russare + bocca
      // oppure solo faccia ferma:
      // Expressions_SetActive(ExprKind::Sleep);
    }
  }

  // ANIMAZIONE ZZZ se in sleep
  if (sSleepMode && sZzzVisible) {
    const unsigned long STEP_MS = 80;  // velocità movimento/animazione
    if (now - sZzzLastStep > STEP_MS) {
      sZzzLastStep = now;

      // sale verso l'alto
      sZzzY -= 1;
      // cambia frame (Z piccola → media → grande → di nuovo)
      sZzzFrame = (uint8_t)((sZzzFrame + 1) % ZZZ_N_FRAMES);

      // se esce dallo schermo in alto, ricomincia
      if (sZzzY < 40) {
        sZzzY = 140;
        sZzzX = 200;
      }
    }
  } else {
    sZzzVisible = false;
  }
}




// --------- ASSET Sleep (dorme + russa) ---------

// occhi chiusi (un solo frame)
static const char* SLEEP_EYES[]  = {
  "occhi_sbadiglia2.bin"
};

// bocca: chiusa → semiaperta → aperta (per respiro/ronfata)
static const char* SLEEP_MOUTH[] = {
  "bocca-dorme.bin",   // chiusa
  "bocca-dorme2.bin",   // semiaperta
  "bocca-dorme3.bin"    // aperta
};

// sequenza bocca durante il russare (apri/chiudi lenta)
static const char* TALK_SLEEP_V1[] = {
  "bocca-dorme.bin",
  "bocca-dorme2.bin",
  "bocca-dorme.bin",
  "bocca-dorme3.bin"
};

// 🔴 CAMBIA 80 con l’ID TRACCIA reale sul DFPlayer (brano di russare)
static const TalkVariant SLEEP_TALKS[] = {
  // track,        seq,             nFrame,                                  frameMs, speechMs
  { 3, TALK_SLEEP_V1, (uint8_t)(sizeof(TALK_SLEEP_V1)/sizeof(TALK_SLEEP_V1[0])),
        350,           2600 } // circa 2,6s per un ciclo di russata
};

static const ExprAssets A_SLEEP = {
  // occhi
  SLEEP_EYES,
  (uint8_t)(sizeof(SLEEP_EYES)/sizeof(SLEEP_EYES[0])),
  "occhi_dorme.bin",         // frame base occhi (chiusi)

  // bocca
  SLEEP_MOUTH,
  (uint8_t)(sizeof(SLEEP_MOUTH)/sizeof(SLEEP_MOUTH[0])),
  "dritta.bin",        // frame base bocca (chiusa, idle)
  
  // TALK (russare)
  SLEEP_TALKS,
  (uint8_t)(sizeof(SLEEP_TALKS)/sizeof(SLEEP_TALKS[0])),

  // --- occhi: niente blink, resta chiuso ---
  60000, 60000,  // eyesOpenMin,  eyesOpenMax  (praticamente mai)
  0,     0,      // eyesBlinkMin, eyesBlinkMax
  0,     0,      // eyesStayPct,  eyesRandPct
  0,             // eyesDoubleBlinkPct

  // --- bocca: piccolo “blink” molto lento, tipo respiro ---
  4000, 6000,    // mouthOpenMin,  mouthOpenMax (ogni 4–6s)
  400,  700,     // mouthBlinkMin, mouthBlinkMax (apertura/chiusura lenta)
  20,   40,      // mouthStayPct,  mouthRandPct
  0,             // mouthDoubleBlinkPct

  0              // speechOffsetMs (puoi aggiustare se serve sync fine)
};



// --------- ASSET Wakeup (risveglio) ---------

// TALK "Buongiorno"
static const char* TALK_WAKE_V1[] = {
  "bocca-dorme.bin",
  "bocca-badiglia3.bin",
  "bocca-dorme.bin",
  "bocca-badiglia3.bin"
};

// 🔴 CAMBIA 81 con il numero reale della traccia "Buongiorno" sul DFPlayer
static const TalkVariant WAKE_TALKS[] = {
  // track, frameSeq,                                   nFrame,                                   frameMs, speechMs
  { 6,    TALK_WAKE_V1, (uint8_t)(sizeof(TALK_WAKE_V1)/sizeof(TALK_WAKE_V1[0])), 140, 1800 }
};

static const char* WAKE_EYES[]  = {
  "occhi_sbadiglia2.bin",   // socchiusi
  "occhi_sbadiglia1.bin",   // più aperti
  "occhi_aperti.bin"        // ben aperti
};

static const char* WAKE_MOUTH[] = {
  "bocca-dorme.bin",
  "bocca-badiglia3.bin"
};

static const ExprAssets A_WAKE = {
  WAKE_EYES,  (uint8_t)(sizeof(WAKE_EYES)/sizeof(WAKE_EYES[0])),   "occhi_sbadiglia1.bin",
  WAKE_MOUTH, (uint8_t)(sizeof(WAKE_MOUTH)/sizeof(WAKE_MOUTH[0])), "bocca-badiglia3.bin",

  // TALK "buongiorno"
  WAKE_TALKS,
  (uint8_t)(sizeof(WAKE_TALKS)/sizeof(WAKE_TALKS[0])),

  // occhi: blink normale
  3000, 5000,
  150,  200,
  15,   45,
  0,

  // bocca: piccolo movimento
  3000, 5000,
  150,  200,
  15,   45,
  0,

  +200   // piccolo offset, regola se serve per sync fine
};


// --------- ASSET LIFTED ---------
static const char* LIF_EYES[]  = { "occhi_cielo.bin","occhi_digrigna.bin","occhi_paura_c.bin","occhi_arrabbiati.bin" };
static const char* LIF_MOUTH[] = { "b_paura.bin","digrigna.bin","bocca_preoccu.bin","O.bin" };

// --------- LIFTED (3 voci) ---------
static const char* TALK_LIF_V1[] = {"A.bin","I.bin","U.bin","T.bin","O.bin","O.bin"}; // 6 frame
static const char* TALK_LIF_V2[] = {"A.bin","I.bin","I.bin"};                          // 3 frame, più secca
static const char* TALK_LIF_V3[] = {"A.bin","R.bin","R.bin","R.bin"};                  // 4 frame, “digrigna”

static const TalkVariant LIF_TALKS[] = {
  { 7, TALK_LIF_V1, (uint8_t)(sizeof(TALK_LIF_V1)/sizeof(TALK_LIF_V1[0])), 140, 1800 },//AIUTO.
  { 48, TALK_LIF_V2, (uint8_t)(sizeof(TALK_LIF_V2)/sizeof(TALK_LIF_V2[0])), 110, 1200 },//AHII.
  { 50, TALK_LIF_V3, (uint8_t)(sizeof(TALK_LIF_V3)/sizeof(TALK_LIF_V3[0])), 120, 1500 },//Arghhhh
};
static const ExprAssets A_LIF = {
  LIF_EYES, (uint8_t)(sizeof(LIF_EYES)/sizeof(LIF_EYES[0])), "occhi_chiusi.bin",
  LIF_MOUTH,(uint8_t)(sizeof(LIF_MOUTH)/sizeof(LIF_MOUTH[0])), "B_paura_c.bin",
  LIF_TALKS,(uint8_t)(sizeof(LIF_TALKS)/sizeof(LIF_TALKS[0])),
  // --- OCCHI (esempio) ---
  4000, 5000,   // eyesOpenMin,  eyesOpenMax
  150,  200,    // eyesBlinkMin, eyesBlinkMax
  15,   45,     // eyesStayPct,  eyesRandPct
  0,            // eyesDoubleBlinkPct

  // --- BOCCA (qui regoli il “blink” della bocca in idle) ---
  3000, 6000,   // mouthOpenMin,  mouthOpenMax   -> meno/ più frequente
  130,  180,    // mouthBlinkMin, mouthBlinkMax  -> più/meno veloce la chiusura
  10,   40,     // mouthStayPct,  mouthRandPct   -> “resta” / “random” variante
  0,            // mouthDoubleBlinkPct           -> colpetto doppio (%)

  +800          // speechOffsetMs (sync audio/bocca)
};










// --- helper: prende track, frameMs e speechMs dalla tabella della specifica espressione
static bool getTalkFor(ExprKind kind, uint8_t variantIndex,
                       int16_t &track, uint16_t &frameMs, uint16_t &speechMs)
{
  if (variantIndex == 0) variantIndex = 1;           // normalizza 1..N

  switch (kind) {
    case ExprKind::Natural: {
      const uint8_t n = (uint8_t)(sizeof(NAT_TALKS)/sizeof(NAT_TALKS[0]));
      const uint8_t i = (variantIndex - 1) % n;
      track    = NAT_TALKS[i].track;
      frameMs  = NAT_TALKS[i].frameMsDefault;   // << usa i nomi GIUSTI
      speechMs = NAT_TALKS[i].speechMsDefault;  // << usa i nomi GIUSTI
      return true;
    }
    case ExprKind::Angry: {
      const uint8_t n = (uint8_t)(sizeof(ANG_TALKS)/sizeof(ANG_TALKS[0]));
      const uint8_t i = (variantIndex - 1) % n;
      track    = ANG_TALKS[i].track;
      frameMs  = ANG_TALKS[i].frameMsDefault;   // <<
      speechMs = ANG_TALKS[i].speechMsDefault;  // <<
      return true;
    }
    case ExprKind::Fear: {
      const uint8_t n = (uint8_t)(sizeof(FEAR_TALKS)/sizeof(FEAR_TALKS[0]));
      const uint8_t i = (variantIndex - 1) % n;
      track    = FEAR_TALKS[i].track;
      frameMs  = FEAR_TALKS[i].frameMsDefault;  // <<
      speechMs = FEAR_TALKS[i].speechMsDefault; // <<
      return true;
    }
    case ExprKind::Greeting: {
      const uint8_t n = (uint8_t)(sizeof(GREET_TALKS)/sizeof(GREET_TALKS[0]));
      const uint8_t i = (variantIndex - 1) % n;
      track    = GREET_TALKS[i].track;
      frameMs  = GREET_TALKS[i].frameMsDefault; // <<
      speechMs = GREET_TALKS[i].speechMsDefault;// <<
      return true;
    }
	case ExprKind::Sadness: {
      const uint8_t n = (uint8_t)(sizeof(SAD_TALKS)/sizeof(SAD_TALKS[0]));
      const uint8_t i = (variantIndex - 1) % n;
      track    = SAD_TALKS[i].track;
      frameMs  = SAD_TALKS[i].frameMsDefault;   // <<
      speechMs = SAD_TALKS[i].speechMsDefault;  // <<
      return true;
    }
   case ExprKind::Yawn: {   // 🔹 NUOVO
      const uint8_t n = (uint8_t)(sizeof(YAWN_TALKS)/sizeof(YAWN_TALKS[0]));
      const uint8_t i = (variantIndex - 1) % n;
      track    = YAWN_TALKS[i].track;
      frameMs  = YAWN_TALKS[i].frameMsDefault;
      speechMs = YAWN_TALKS[i].speechMsDefault;
      return true;
    }
	case ExprKind::Sleep: {
  const uint8_t n = (uint8_t)(sizeof(SLEEP_TALKS)/sizeof(SLEEP_TALKS[0]));
  const uint8_t i = (variantIndex - 1) % n;
  track    = SLEEP_TALKS[i].track;
  frameMs  = SLEEP_TALKS[i].frameMsDefault;
  speechMs = SLEEP_TALKS[i].speechMsDefault;
  return true;
}
case ExprKind::Wakeup: {
  const uint8_t n = (uint8_t)(sizeof(WAKE_TALKS)/sizeof(WAKE_TALKS[0]));
  const uint8_t i = (variantIndex - 1) % n;
  track    = WAKE_TALKS[i].track;
  frameMs  = WAKE_TALKS[i].frameMsDefault;
  speechMs = WAKE_TALKS[i].speechMsDefault;
  return true;
}
    case ExprKind::Lifted: {
      const uint8_t n = (uint8_t)(sizeof(LIF_TALKS)/sizeof(LIF_TALKS[0]));
      const uint8_t i = (variantIndex - 1) % n;
      track    = LIF_TALKS[i].track;
      frameMs  = LIF_TALKS[i].frameMsDefault;
      speechMs = LIF_TALKS[i].speechMsDefault;
      return true;
    }

  }
  return false;
}


// --- API comoda: scegli la variante 1..N e parte con i tempi corretti della tabella
void Expressions_PlayVariant(ExprKind kind, uint8_t variantIndex) {
  int16_t  track = 0;
  uint16_t frameMs = 0, speechMs = 0;
  if (getTalkFor(kind, variantIndex, track, frameMs, speechMs)) {
    Expressions_Play(kind, track, frameMs, speechMs);
  } else {
    Expressions_Play(kind, 0 /*track default*/, 0 /*frameMs*/, 0 /*speechMs*/);
  }
}








// --------- STATI ----------
static ExprState S_NAT, S_ANG, S_FEAR, S_GRE, S_SAD;
static ExprState S_SLEEP, S_WAKE, S_YAWN, S_LIF;
static ExprKind gActive = ExprKind::Natural;

static inline const ExprAssets& assetsOf(ExprKind k){
  switch(k){
    case ExprKind::Angry:  return A_ANG;
    case ExprKind::Fear:   return A_FEAR;
	case ExprKind::Greeting: return A_GREET;
	case ExprKind::Sadness: return A_SAD;
    case ExprKind::Sleep:    return A_SLEEP; // 🔹
    case ExprKind::Wakeup:   return A_WAKE;  // 🔹
    case ExprKind::Yawn:     return A_YAWN;  // 🔹
	case ExprKind::Lifted: return A_LIF; 
	default:               return A_NAT;
  }
}
static inline ExprState& stateOf(ExprKind k){
  switch(k){
    case ExprKind::Angry:  return S_ANG;
    case ExprKind::Fear:   return S_FEAR;
	case ExprKind::Greeting: return S_GRE;
	case ExprKind::Sadness: return S_SAD;
    case ExprKind::Sleep:    return S_SLEEP; // 🔹
    case ExprKind::Wakeup:   return S_WAKE;  // 🔹
    case ExprKind::Yawn:     return S_YAWN;  // 🔹
	case ExprKind::Lifted: return S_LIF;
	default:               return S_NAT;
  }
}

// --------- API MANAGER ----------
void Expressions_Init(){
	// seed robusto da TRNG hardware
  randomSeed((uint32_t)esp_random());
  Expr_Init(A_NAT,  S_NAT);
  Expr_Init(A_ANG,  S_ANG);
  Expr_Init(A_FEAR, S_FEAR);
  Expr_Init(A_GREET,S_GRE); 
  Expr_Init(A_SAD,S_SAD); 
  Expr_Init(A_SLEEP,S_SLEEP);  // 🔹
  Expr_Init(A_WAKE, S_WAKE);   // 🔹
  Expr_Init(A_YAWN, S_YAWN);   // 🔹
  Expr_Init(A_LIF, S_LIF);
  gActive = ExprKind::Natural;
  sLastActivityMs = millis();
  sSleepMode = false;
sYawnDone  = false;
  AuraEnsureInit();
  gAura.update(millis(), gActive);   // set effetto iniziale
    TtpEnsureInit();


}

static ExprKind NextExprKind(ExprKind cur) {
  // ciclo SOLO tra quelle che hai già definite e usi davvero
  switch (cur) {
    case ExprKind::Natural:  return ExprKind::Angry;
    case ExprKind::Angry:    return ExprKind::Fear;
    case ExprKind::Fear:     return ExprKind::Greeting;
    case ExprKind::Greeting: return ExprKind::Sadness;
    case ExprKind::Sadness:  return ExprKind::Natural;

    // se sei in modalità speciali, al tap torna al giro base
    case ExprKind::Yawn:
    case ExprKind::Wakeup:
    case ExprKind::Sleep:
    case ExprKind::Sing:
	case ExprKind::Lifted: 
    default:
      return ExprKind::Natural;
  }
}

void Expressions_Update(unsigned long now){
  Expr_Update(assetsOf(gActive), stateOf(gActive), now);

  // Aura: se boot in corso, NON aggiornare qui (ci pensa Aura_BootTick)
  AuraEnsureInit();
  if (!Aura_BootIsRunning()) {
    gAura.update(now, gActive);
  }

  // TTP223
  TtpEnsureInit();
  TtpEvent ev;
  if (gTtp.update(now, ev)) {
    // qualsiasi gesto = attività utente (sveglia da sleep + reset idle timer)
    Expressions_NotifyUserActivity(now);

    switch (ev.type) {
      case TtpEvent::Tap: {
           TtpBeepTap();       
	   ExprKind next = NextExprKind(gActive);
        Expressions_SetActive(next);              // cambio “solo faccia/idle”
        // se vuoi anche una vocina ogni tap:
        // Expressions_PlayVariant(next, 1);
      } break;

      case TtpEvent::DoubleTap:
	       TtpBeepDouble();
        Expressions_PlayVariant(ExprKind::Sing, 1);
        break;

      case TtpEvent::LongPress:
	       TtpBeepLong();
        if (gActive != ExprKind::Sleep) {
          Expressions_PlayVariant(ExprKind::Sleep, 1);
        } else {
          Expressions_PlayVariant(ExprKind::Wakeup, 1);
        }
        break;
    }
  }
}



void Expressions_SetActive(ExprKind kind){
  if (gActive == kind) {
    // stesso preset: re-apply asset e idle pulito
    stateOf(kind).activeAssetsSet = false;
    Expr_Stop(assetsOf(kind), stateOf(kind));          // porta in idle del preset
    Expr_ApplyAssetsNow(assetsOf(kind), stateOf(kind)); // <-- vedi helper sotto
    AuraEnsureInit();
if (gActive == ExprKind::Sleep) gAura.setBrightness(30);
else                           gAura.setBrightness(80);
gAura.update(millis(), gActive);
	return;
  }

  // 1) stop del PRECEDENTE (quello attualmente attivo)
  Expr_Stop(assetsOf(gActive), stateOf(gActive));

  // 2) switch
  gActive = kind;
AuraEnsureInit();
if (gActive == ExprKind::Sleep) gAura.setBrightness(30);
else                           gAura.setBrightness(80);
gAura.update(millis(), gActive);
  stateOf(kind).activeAssetsSet = false;

  // 3) re-apply asset del nuovo + primo frame subito
  Expr_ApplyAssetsNow(assetsOf(kind), stateOf(kind));
}



void Expressions_Play(ExprKind kind, int16_t track, uint16_t frameMsOverride, uint16_t speechMsOverride){
  if (gActive != kind) {
    // stop pulito del precedente prima di cambiare
    Expr_Stop(assetsOf(gActive), stateOf(gActive));
    gActive = kind;

    AuraEnsureInit();
    gAura.update(millis(), gActive);

    stateOf(kind).activeAssetsSet = false;
    Expr_ApplyAssetsNow(assetsOf(kind), stateOf(kind));  // primo frame del nuovo
  }

  // ✅ 1) AVVIA PRIMA l'espressione (audio + mouth-seq)
  Expr_Play(assetsOf(kind), stateOf(kind), track, frameMsOverride, speechMsOverride);

  // ✅ 2) Se Lifted: forza un refresh schermo prima del movimento bloccante
  if (kind == ExprKind::Lifted) {
    unsigned long t = millis();
    Faces_LvglLoop();
    updateFaces(t);
    Faces_SyncUpdate(t);

    // ✅ 3) ORA fai il movimento gambe (bloccante)
    Lifted_MoveLegs();
  }
}



void Expressions_Stop(ExprKind kind){
  Expr_Stop(assetsOf(kind), stateOf(kind));
  // se ho fermato l’attivo, resta in idle su quel preset
}

ExprKind Expressions_GetActive(){ return gActive; }



uint8_t PickRandomTrackFor(const String& who) {
  static uint8_t iMarco=0, iFra=0, iUnk=0;

  String w = who; w.toLowerCase();

  if (w == "marco") {
    static const uint8_t k[] = {39, 51, 54};
    uint8_t t = k[iMarco % 3]; iMarco++;
    return t;
  }
  if (w == "francesco") {
    static const uint8_t k[] = {38, 55, 41};
    uint8_t t = k[iFra % 3]; iFra++;
    return t;
  }
  // unknown / altri
  static const uint8_t k[] = {46, 47, 54};
  uint8_t t = k[iUnk % 3]; iUnk++;
  return t;
}

// Applica subito gli asset del preset e disegna il primo frame idle coerente
static void Expr_ApplyAssetsNow(const ExprAssets& A, ExprState& S){
  // Forza FaceBlink ad usare gli asset correnti (eyes open/closed + mouth open/closed)
  // Usa la tua funzione già esistente che imposta assets e (se vuoi) abilita i blink.
 Faces_BlinkSetAssets_C(
    A.eyesOpen,  A.nEyesOpen,     // << nome corretto
    A.eyesClosed,
    A.mouthOpen, A.nMouthOpen,    // << nome corretto
    A.mouthBlink,                 // << nome corretto del bin “chiuso”
    /*drawFirstFrame=*/true,
    /*enableEyesBlink=*/true,
    /*enableMouthBlink=*/true,
    /*resetPhases=*/true
);


  S.activeAssetsSet = true;

  // Disegna subito un frame coerente d’idle (occhi open + bocca idle)
  Faces_ShowEyes(/*closed=*/false);
  Faces_ShowMouth();
}

