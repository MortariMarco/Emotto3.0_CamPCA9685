// ExprEngine.cpp
#include "ExprEngine.h"
#include "Faces.h"
#include "FaceBlink.h"
#include "DFRobotDFPlayerMini.h"
#include "HWCDC.h"

extern DFRobotDFPlayerMini dfplayer;
extern HWCDC USBSerial;

bool gForcePreloadTalkFrames = false;

// forward locali
static void shuffle(uint8_t* a, uint8_t n){ for(int i=n-1;i>0;--i){ int j=random(i+1); uint8_t t=a[i]; a[i]=a[j]; a[j]=t; } }
static uint8_t copyFrames(const char* const* src,uint8_t srcCount,const char** dst,uint8_t dstMax){
  uint8_t n = (srcCount>dstMax)?dstMax:srcCount; for(uint8_t i=0;i<n;i++) dst[i]=src[i]; return n;
}

uint16_t Expr_LoopsFromDuration(uint16_t frameMs, uint8_t frameCount, uint16_t speechMs) {
  if (!frameMs || !frameCount) return 1;
  const uint32_t cycle = (uint32_t)frameMs * frameCount;
  uint32_t L = (speechMs + cycle/2) / cycle;
  if (L == 0) L = 1; if (L > 30000) L = 30000;
  return (uint16_t)L;
}

static int findTalkByTrack(const ExprAssets& A, int16_t t){
  if (t < 0 || A.nTalks==0) return -1;
  for(uint8_t i=0;i<A.nTalks;i++) if(A.talks[i].track == t) return i;
  return -1;
}

static void eyesOrderInit(ExprState& S, const ExprAssets& A){
  S.eyeOrderLen = (A.nEyesOpen > 8 ? 8 : A.nEyesOpen);
  for(uint8_t i=0;i<S.eyeOrderLen;i++) S.eyeOrder[i]=i;
  shuffle(S.eyeOrder,S.eyeOrderLen);
  S.eyePos=0; S.eyeCurIdx=S.eyeOrder[S.eyePos];
}
static void mouthOrderInit(ExprState& S, const ExprAssets& A){
  S.mouthOrderLen = (A.nMouthOpen > 8 ? 8 : A.nMouthOpen);
  for(uint8_t i=0;i<S.mouthOrderLen;i++) S.mouthOrder[i]=i;
  shuffle(S.mouthOrder,S.mouthOrderLen);
  S.mouthPos=0; S.mouthCurIdx=S.mouthOrder[S.mouthPos];
}

static void applyActiveAssets(const ExprAssets& A) {
  Faces_BlinkSetAssetsRaw(
      A.eyesOpen,   A.nEyesOpen,
      A.eyesClosed,
      A.mouthOpen,  A.nMouthOpen,
      A.mouthBlink,
      /*drawFirstFrame=*/true,
      /*enableEyesBlink=*/true,
      /*enableMouthBlink=*/true,
      /*resetPhases=*/true
  );

  Faces_SetBlinkParamsEyes(
      A.eyesOpenMin,   A.eyesOpenMax,
      A.eyesBlinkMin,  A.eyesBlinkMax,
      A.eyesStayPct,   A.eyesRandPct,
      A.eyesDoubleBlinkPct
  );

  Faces_SetBlinkParamsMouth(
      A.mouthOpenMin,   A.mouthOpenMax,
      A.mouthBlinkMin,  A.mouthBlinkMax,
      A.mouthStayPct,   A.mouthRandPct,
      A.mouthDoubleBlinkPct
  );

  FaceBlink::Enable(true, true);
  FaceBlink::ResetPhases(millis());

}

void Expr_Init(const ExprAssets& A, ExprState& S){
  S = ExprState{};               // reset pulito
  S.activeAssetsSet = false;     // <<— NON applica ancora gli asset
}


void Expr_Stop(const ExprAssets& A, ExprState& S){
  Faces_StopMouthSequence();
  dfplayer.stop();
  FaceBlink::LockEyes(false);
  FaceBlink::LockMouth(false);
  S.soundPending=false; S.pendingTrack=-1; S.phaseUntil=0;
  S.mode = ExprState::Idle;
  if (!S.activeAssetsSet) { applyActiveAssets(A); S.activeAssetsSet=true; }
}

void Expr_Play(const ExprAssets& A, ExprState& S,
               int16_t track, uint16_t frameMsOverride, uint16_t speechMsOverride)
{
  if (S.mode != ExprState::Idle) Expr_Stop(A,S);
  if (!S.activeAssetsSet) { applyActiveAssets(A); S.activeAssetsSet = true; }

  int pick = -1;
  if (track == 0) { // ESPR_TRACK_DEFAULT
    pick = (A.nTalks ? random(A.nTalks) : -1);
    if (pick >= 0) track = A.talks[pick].track;
  } else if (track > 0) {
    pick = findTalkByTrack(A, track);
    if (pick < 0 && A.nTalks) { pick = random(A.nTalks); track = A.talks[pick].track; }
  }

  S.talkIdx      = (uint8_t)((pick>=0)?pick:0);
  uint16_t fDef  = (pick>=0)?A.talks[pick].frameMsDefault:150;
  uint16_t sDef  = (pick>=0)?A.talks[pick].speechMsDefault:2000;
  S.frameMsEff   = (frameMsOverride  ? frameMsOverride  : fDef);
  S.speechMsEff  = (speechMsOverride ? speechMsOverride : sDef);

  FaceBlink::Enable(true, false);

  eyesOrderInit(S, A);
  mouthOrderInit(S, A);

  uint8_t fcount = 0;
  if (pick >= 0 && A.talks[pick].frames && A.talks[pick].count) {
    const char* framesBuf[10];
    fcount = copyFrames(
      A.talks[pick].frames,
      A.talks[pick].count,
      framesBuf,
      (uint8_t)(sizeof(framesBuf) / sizeof(framesBuf[0]))
    );

    // Preload: normalmente evita quando CAM busy.
    // In boot puoi forzare (gForcePreloadTalkFrames = true) per evitare scatti.
    if (gForcePreloadTalkFrames || !Faces_CamIsBusy()) {
      (void)Faces_PreloadMouthSequence(framesBuf, (int)fcount);
    }
  }



  const uint16_t loops = Expr_LoopsFromDuration(S.frameMsEff, fcount?fcount:1, S.speechMsEff);

  uint32_t audioDelay = 0, mouthDelay = 0;
  if (A.speechOffsetMs >= 0) mouthDelay += (uint32_t)A.speechOffsetMs;
  else                       audioDelay += (uint32_t)(-A.speechOffsetMs);

  Faces_ArmMouthSequenceAfterDelay(S.frameMsEff, loops, (uint16_t)mouthDelay);

  if (track > 0) {
    S.soundPending = true; S.pendingTrack = track; S.soundStartAt = millis() + audioDelay;
  }

  S.phaseUntil = millis() + mouthDelay + (uint32_t)S.frameMsEff * (fcount?fcount:1) * loops + 50;
  S.mode       = ExprState::Talk;

  S.eyeNextTs   = millis() + random(A.eyesOpenMin, A.eyesOpenMax);
  S.mouthNextTs = 0;
}

void Expr_Update(const ExprAssets& A, ExprState& S, unsigned long now){
  Faces_SyncUpdate(now);

  if (S.soundPending && (int32_t)(now - (int32_t)S.soundStartAt) >= 0) {
    dfplayer.play(S.pendingTrack);
    S.soundPending=false; S.pendingTrack=-1;
  }

  if (S.mode == ExprState::Talk || S.mode == ExprState::IdleLoop) {
    if ((int32_t)(now - (int32_t)S.eyeNextTs) >= 0) {
      uint8_t nextIdx = S.eyeCurIdx;
      uint8_t dice = random(100);
      if (dice < A.eyesStayPct) {
        // keep
      } else if (dice < (uint8_t)(A.eyesStayPct + A.eyesRandPct)) {
        if (A.nEyesOpen > 1) { do { nextIdx = (uint8_t)random(A.nEyesOpen); } while (nextIdx == S.eyeCurIdx); }
      } else {
        S.eyePos++; if (S.eyePos >= S.eyeOrderLen) eyesOrderInit(S,A); nextIdx = S.eyeOrder[S.eyePos];
      }
      if (nextIdx != S.eyeCurIdx) {
        S.eyeCurIdx = nextIdx;
        setEyesURLs(A.eyesOpen[nextIdx], A.eyesClosed);
      }
      S.eyeNextTs = now + random(A.eyesOpenMin, A.eyesOpenMax);
    }
  }

  if (S.mode == ExprState::IdleLoop) {
    if (S.mouthNextTs == 0) S.mouthNextTs = now + random(A.mouthOpenMin, A.mouthOpenMax);
    if ((int32_t)(now - (int32_t)S.mouthNextTs) >= 0) {
      uint8_t nextIdx = S.mouthCurIdx;
      uint8_t dice = random(100);
      if (dice < A.mouthStayPct) {
        // keep
      } else if (dice < (uint8_t)(A.mouthStayPct + A.mouthRandPct)) {
        if (A.nMouthOpen > 1) { do { nextIdx = (uint8_t)random(A.nMouthOpen); } while (nextIdx == S.mouthCurIdx); }
      } else {
        S.mouthPos++; if (S.mouthPos >= S.mouthOrderLen) mouthOrderInit(S,A); nextIdx = S.mouthOrder[S.mouthPos];
      }
      if (nextIdx != S.mouthCurIdx) {
        S.mouthCurIdx = nextIdx;
        setMouthURL(A.mouthOpen[nextIdx]);
      }
      S.mouthNextTs = now + random(A.mouthOpenMin, A.mouthOpenMax);
    }
  }

  if (S.mode == ExprState::Talk && (int32_t)(now - (int32_t)S.phaseUntil) >= 0) {
    Faces_StopMouthSequence();

    // ✅ torna SUBITO alla bocca idle (sorriso), niente frame appiccicato
    Faces_ShowMouth();

    FaceBlink::Enable(true, true);
    FaceBlink::ResetPhases(millis());

    S.mode        = ExprState::IdleLoop;
    S.phaseUntil  = 0;
    S.mouthNextTs = now + 80; // riparte presto, non dopo 3–6s
  }
}

