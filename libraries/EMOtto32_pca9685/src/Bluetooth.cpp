// Bluetooth.cpp — EMOtto 3.0 BLE (Nordic UART / NUS)
// Comandi: PING / EXPR / EXPRV / VOL / MOVE / SPEED (+/-)
// TX notify: EVT|BLE|CONNECTED, EVT|VOL|x, EVT|SPEED|ms
//
// NOTE:
// - RX (write) = 6E400002...
// - TX (notify)= 6E400003...
// - Le write possono arrivare "a pezzi": usiamo buffer + parsing a '\n'

#include "Bluetooth.h"

#include <Arduino.h>
#include <vector>

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include "wifiCam.h"

#include "StatusBar.h"
#include "espressioni.h"
#include <EMOtto.h>
#include <DFRobotDFPlayerMini.h>

// Oggetti globali già nel tuo progetto
extern Otto Otto;
extern DFRobotDFPlayerMini dfplayer;
extern HWCDC USBSerial;

// UUID Nordic UART
static const char* kSvcUUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
static const char* kRxUUID  = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";
static const char* kTxUUID  = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";

static BLEServer*         s_server = nullptr;
static BLEService*        s_svc    = nullptr;
static BLECharacteristic* s_rx     = nullptr;
static BLECharacteristic* s_tx     = nullptr;
static bool               s_connected = false;

// Movimento continuo
static bool s_walkFwd  = false;
static bool s_walkBack = false;
static bool s_turnL    = false;
static bool s_turnR    = false;

static int      s_walkSpeedMs = 1000;  // 200..2000
static uint32_t s_lastStepMs  = 0;

// Volume gestito qui (senza cambiare il tuo .ino)
static int sVolume = 18;

// RX buffer riga (accumula fino a '\n')
static String s_rxLine;

// ----------------- Utils -----------------
static int clampi(int v, int lo, int hi) {
  return (v < lo) ? lo : (v > hi) ? hi : v;
}

static std::vector<String> splitPipe(const String& s) {
  std::vector<String> out;
  int start = 0;
  while (true) {
    int i = s.indexOf('|', start);
    if (i < 0) { out.push_back(s.substring(start)); break; }
    out.push_back(s.substring(start, i));
    start = i + 1;
  }
  return out;
}

static void bleSendLine(const String& line) {
  if (!s_tx) {
    USBSerial.println("[BLE TX] SKIP (s_tx null)");
    return;
  }
  if (!s_connected) {
    USBSerial.println("[BLE TX] SKIP (not connected)");
    return;
  }

  String out = line;
  if (!out.endsWith("\n")) out += "\n";

  // DEBUG: vedi sempre cosa notifichi
  USBSerial.print("[BLE TX] ");
  USBSerial.print(out);

  s_tx->setValue((uint8_t*)out.c_str(), out.length());
  s_tx->notify();
}

static void stopAllMove(bool home) {
  s_walkFwd = s_walkBack = s_turnL = s_turnR = false;
  if (home) Otto.home();
}

// -------- ExprKind parser --------
static bool parseExprKind(String s, ExprKind &k) {
  s.trim();
  s.toLowerCase();

  if (s=="natural"||s=="nat")      { k=ExprKind::Natural;  return true; }
  if (s=="angry"||s=="ang")        { k=ExprKind::Angry;    return true; }
  if (s=="fear")                   { k=ExprKind::Fear;     return true; }
  if (s=="greeting"||s=="greet")   { k=ExprKind::Greeting; return true; }
  if (s=="sad"||s=="sadness")      { k=ExprKind::Sadness;  return true; }
  if (s=="yawn")                   { k=ExprKind::Yawn;     return true; }
  if (s=="sleep")                  { k=ExprKind::Sleep;    return true; }
  if (s=="wakeup"||s=="wake")      { k=ExprKind::Wakeup;   return true; }

  return false;
}

// ----------------- Command handler -----------------
// PING
// EXPR|Natural
// EXPRV|Natural|1
// VOL         -> query (ritorna valore attuale)
// VOL|18      -> set + ritorna valore
// SPEED|1200   oppure SPEED+ / SPEED-
// MOVE|FWD/BACK/LEFT/RIGHT/STOP/HOME
static void handleCmdLine(String line) {
	
  line.trim();
  if (line.isEmpty()) return;
 // ---------- CAM commands (plain text) ----------
// Permette di inviare via BLE: "cam stream on", "cam stream url", "cam view on", ecc.
{
  String out;
  if (wifiCamHandleCmd(line, &out)) {
    out.trim();

    // 1) Se contiene un URL, estrai SOLO l’URL (o addirittura solo host:port)
    //    esempio out: "STREAM ON OK | http://192.168.4.1:81"
    int p = out.indexOf("http://");
    if (p < 0) p = out.indexOf("https://");
    if (p >= 0) {
      out = out.substring(p);     // out = "http://192.168.4.1:81"
      out.replace("http://", ""); // out = "192.168.4.1:81"  (più corto, non spezza BLE)
      out.replace("https://", "");
    }

    // 2) Manda SOLO testo, senza EVT|...
    //    (aggiunge \n automaticamente dentro bleSendLine)
    bleSendLine(out.length() ? out : "OK");
    return;
  }
}


  USBSerial.print("[BLE CMD] ");
  USBSerial.println(line);

  auto tok = splitPipe(line);
  if (tok.empty()) return;

  String cmd = tok[0];
  cmd.trim();
  cmd.toUpperCase();

  // ogni comando = attività utente
  Expressions_NotifyUserActivity(millis());

  if (cmd == "PING") {
    bleSendLine("OK|PING");
    return;
  }

  if (cmd == "EXPR" && tok.size() >= 2) {
    ExprKind k;
    if (!parseExprKind(tok[1], k)) { bleSendLine("ERR|EXPR|BAD"); return; }
    Expressions_SetActive(k);
    bleSendLine("OK|EXPR|" + tok[1]);
    return;
  }

  if (cmd == "EXPRV" && tok.size() >= 3) {
    ExprKind k;
    if (!parseExprKind(tok[1], k)) { bleSendLine("ERR|EXPRV|BAD"); return; }

    long vv = tok[2].toInt();
    if (vv < 1) vv = 1;
    if (vv > 255) vv = 255;
    uint8_t v = (uint8_t)vv;

    Expressions_PlayVariant(k, v);
    bleSendLine("OK|EXPRV|" + tok[1] + "|" + String(v));
    return;
  }

  // ✅ VOL query + set
  if (cmd == "VOL") {
    if (tok.size() >= 2) {
      sVolume = clampi(tok[1].toInt(), 0, 30);
      dfplayer.volume(sVolume);
    }
    bleSendLine("EVT|VOL|" + String(sVolume));
    return;
  }
  if (cmd == "VOL+") {
    sVolume = clampi(sVolume + 1, 0, 30);
    dfplayer.volume(sVolume);
    bleSendLine("OK|VOL|" + String(sVolume));
    bleSendLine("EVT|VOL|" + String(sVolume));
    return;
  }

  if (cmd == "VOL-") {
    sVolume = clampi(sVolume - 1, 0, 30);
    dfplayer.volume(sVolume);
    bleSendLine("OK|VOL|" + String(sVolume));
    bleSendLine("EVT|VOL|" + String(sVolume));
    return;
  }

 if (cmd == "SPEED") {
  // se arriva SPEED|1200 => set
  if (tok.size() >= 2) {
    s_walkSpeedMs = clampi(tok[1].toInt(), 200, 2000);
  }
  // in ogni caso rispondi con il valore attuale
  bleSendLine("EVT|SPEED|" + String(s_walkSpeedMs));
  return;
}

  if (cmd == "SPEED+") {
    s_walkSpeedMs = clampi(s_walkSpeedMs - 100, 200, 2000);
    bleSendLine("EVT|SPEED|" + String(s_walkSpeedMs));
    return;
  }
  if (cmd == "SPEED-") {
    s_walkSpeedMs = clampi(s_walkSpeedMs + 100, 200, 2000);
    bleSendLine("EVT|SPEED|" + String(s_walkSpeedMs));
    return;
  }

  if (cmd == "MOVE" && tok.size() >= 2) {
    String m = tok[1]; m.trim(); m.toUpperCase();

    if (m=="FWD")   { s_walkFwd=true;  s_walkBack=s_turnL=s_turnR=false; bleSendLine("OK|MOVE|FWD");   return; }
    if (m=="BACK")  { s_walkBack=true; s_walkFwd=s_turnL=s_turnR=false; bleSendLine("OK|MOVE|BACK");  return; }
    if (m=="LEFT")  { s_turnL=true;    s_walkFwd=s_walkBack=s_turnR=false; bleSendLine("OK|MOVE|LEFT");  return; }
    if (m=="RIGHT") { s_turnR=true;    s_walkFwd=s_walkBack=s_turnL=false; bleSendLine("OK|MOVE|RIGHT"); return; }

    // ✅ STOP = HOME
    if (m=="STOP")  { stopAllMove(true); bleSendLine("OK|MOVE|STOP"); return; }
    if (m=="HOME")  { stopAllMove(true); bleSendLine("OK|MOVE|HOME"); return; }

    bleSendLine("ERR|MOVE|BAD");
    return;
  }

  bleSendLine("ERR|BAD_CMD");
}

// ----------------- BLE callbacks -----------------
class RxCb : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) override {
    auto sv = c->getValue();
    String v = String(sv.c_str());
    if (v.isEmpty()) return;

    USBSerial.print("[BLE RX RAW] ");
    USBSerial.println(v);

    for (size_t i = 0; i < (size_t)v.length(); i++) {
      char ch = v.charAt((unsigned)i);
      if (ch == '\r') continue;

      if (ch == '\n') {
        if (!s_rxLine.isEmpty()) {
          String line = s_rxLine;
          s_rxLine = "";

          USBSerial.print("[BLE RX LINE] ");
          USBSerial.println(line);

          handleCmdLine(line);
        }
      } else {
        if (s_rxLine.length() < 200) s_rxLine += ch;
        else { s_rxLine = ""; bleSendLine("ERR|LINE_TOO_LONG"); }
      }
    }
  }
};

class ServerCb : public BLEServerCallbacks {
  void onConnect(BLEServer*) override {
    s_connected = true;
    StatusLVGL_SetBLE(true);

    USBSerial.println("[BLE] Connected");

    // Sync immediato verso app
    bleSendLine("EVT|BLE|CONNECTED");
    bleSendLine("EVT|VOL|"   + String(sVolume));
    bleSendLine("EVT|SPEED|" + String(s_walkSpeedMs));
  }

  void onDisconnect(BLEServer* s) override {
    s_connected = false;
    StatusLVGL_SetBLE(false);

    USBSerial.println("[BLE] Disconnected");

    stopAllMove(false);
    s->getAdvertising()->start();
  }
};

// ----------------- API pubblica -----------------
void initBLE() {
  BLEDevice::init("EMOtto3");

  s_server = BLEDevice::createServer();
  s_server->setCallbacks(new ServerCb());

  s_svc = s_server->createService(BLEUUID(kSvcUUID));

  // TX notify
  s_tx = s_svc->createCharacteristic(BLEUUID(kTxUUID),
                                     BLECharacteristic::PROPERTY_NOTIFY);
  s_tx->addDescriptor(new BLE2902());

  // RX write
  s_rx = s_svc->createCharacteristic(
    BLEUUID(kRxUUID),
    BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
  );
  s_rx->setCallbacks(new RxCb());

  s_svc->start();

  BLEAdvertising* adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(BLEUUID(kSvcUUID));
  adv->setScanResponse(true);
  adv->setMinPreferred(0x06);
  adv->setMinPreferred(0x12);
  BLEDevice::startAdvertising();

  USBSerial.println("[BLE] Advertising started (EMOtto3 / NUS)");
  StatusLVGL_SetBLE(false);
}

void loopBLE() {}
bool bleIsConnected() { return s_connected; }

// Movimento continuo: chiamala nel loop
void Ble_UpdateWalking(unsigned long now) {
  if (!s_connected) {
    if (s_walkFwd || s_walkBack || s_turnL || s_turnR) stopAllMove(false);
    return;
  }

  if (!(s_walkFwd || s_walkBack || s_turnL || s_turnR)) return;

  // Otto.walk/turn sono bloccanti ~stepMs: chiama un ciclo ogni stepMs
  if ((uint32_t)(now - s_lastStepMs) < (uint32_t)s_walkSpeedMs) return;
  s_lastStepMs = now;

  if (s_walkFwd)  Otto.walk(1, s_walkSpeedMs,  1);
  if (s_walkBack) Otto.walk(1, s_walkSpeedMs, -1);
  if (s_turnL)    Otto.turn(1, s_walkSpeedMs, -1);
  if (s_turnR)    Otto.turn(1, s_walkSpeedMs,  1);
}

// Getter
int Ble_GetSpeedMs() { return s_walkSpeedMs; }
int Ble_GetVolume()  { return sVolume; }
