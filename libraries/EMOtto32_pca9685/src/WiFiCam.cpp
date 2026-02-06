//WIFICam.cpp
#include "wifiCam.h"
#include <WiFi.h>
#include "HWCDC.h"
#include <HardwareSerial.h>
#include <WiFiClient.h>
#include <JPEGDecoder.h>        // installa la lib "JPEGDecoder" (Bodmer)
#include "Arduino_GFX_Library.h"
#include <lvgl.h>
#include "Faces.h"

#ifndef VIEW_CLEAR_COLOR
#define VIEW_CLEAR_COLOR 0x0000   // colore di sfondo: nero (RGB565). Cambia se usi un altro bg.
#endif

extern HWCDC USBSerial;
extern Arduino_GFX *gfx;

// Synchronize UART busy with Faces global guard
extern void Faces_CamBusyBegin(void);
extern void Faces_CamBusyEnd(void);

// ======== Stato viewer /jpeg ========
static bool     s_viewOn      = false;
static String   s_host        = "192.168.4.1";
static uint16_t s_port        = 81;
static uint32_t s_nextPullMs  = 0;
static uint16_t s_targetW     = 240;   // adatta alla tua LCD
static uint16_t s_targetH     = 240;   // adatta alla tua LCD
static uint16_t s_posX        = 60;     // dove disegnare
static uint16_t s_posY        = 40;
bool camViewIsOn() { return s_viewOn; }
static uint8_t  s_viewRot = 0;  // 0,1,2,3 => 0/90/180/270°




// ================== Stato globale ==================
static bool            s_wifiConnected = false;
static HardwareSerial* s_camLink       = nullptr;
static uint32_t        s_camBaud       = 2000000;
static int             s_camUartNum    = 1;

// --- timeouts rapidi per evitare blocchi percepibili ---
#ifndef CAM_UART_HEADER_TIMEOUT_MS
#define CAM_UART_HEADER_TIMEOUT_MS 1500   // attesa "OK <len>"
#endif
#ifndef CAM_UART_SILENCE_TIMEOUT_MS
#define CAM_UART_SILENCE_TIMEOUT_MS 2500  // silenzio mentre leggo i dati
#endif
#ifndef CAM_UART_TAIL_TIMEOUT_MS
#define CAM_UART_TAIL_TIMEOUT_MS 800     // attesa "DONE"
#endif

// ===== Enrollment manager (DISPLAY) =====
static struct {
  bool    active        = false;   // enroll mode ON
  bool    waitingUnknown= false;   // abbiamo visto "unknown", in attesa scelta utente
  bool    awaitingName  = false;   // utente ha detto "sì", attendiamo ENROLL NAME <nome>
  String  lastWho       = "none";  // ultima WHO normalizzata
  unsigned long lastPoll= 0;       // per rate-limit WHO
} gEnroll;

bool Enroll_IsActive_Display() { return gEnroll.active; }

// helpers
static inline String normWho(const String& raw){
  String s = raw; s.trim(); s.toLowerCase();
  if (s == "none" || s == "unknown") return s;
  // scarta sporcizia/linee spurie
  for (size_t i=0;i<s.length();++i) {
    char c=s[i];
    if (c<32 || c>126) return String("none");
  }
  return s;
}

// ================== Helper UART ==================
static bool s_camBusy = false;

static void camLinkFlushRx(uint32_t ms = 8) {
  uint32_t t0 = millis();
  while (millis() - t0 < ms) {
    while (s_camLink && s_camLink->available()) s_camLink->read();
    delay(1);
  }
}

void camViewSetRotation(uint8_t rot90) {
  s_viewRot = rot90 & 0x03;
  USBSerial.printf("[VIEW] rotation = %u * 90deg\n", s_viewRot);
}

// Legge una riga terminata da '\n' (tollerante a \r\n)
static bool readLine(Stream& s, String& line, uint32_t timeoutMs = 80) {
  line = "";
  uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    while (s.available()) {
      int c = s.read();
      if (c < 0) break;
      if (c == '\n') { line.trim(); return true; }
      line += (char)c;
    }
    delay(1);
  }
  return false;
}

// accetta "foo.bin" o "/dir/foo.bin"; rifiuta http/https; forza leading '/'
static String sanitizeBinPath(const String& inRaw) {
  String p = inRaw;
  p.trim();
  if (p.isEmpty()) return String();

  if (p.startsWith("http://") || p.startsWith("https://")) return String();

  for (size_t i = 0; i < p.length(); i++) {
    char ch = p[i];
    bool ok = (ch == '/') || (ch == '.') || (ch == '_') || (ch == '-') ||
              (ch >= '0' && ch <= '9') ||
              (ch >= 'A' && ch <= 'Z') ||
              (ch >= 'a' && ch <= 'z');
    if (!ok) return String();
  }

  if (!p.endsWith(".bin")) return String();

  if (p[0] != '/') p = "/" + p;
  return p;
}

// ================== API ==================

bool initWiFiCam(const char* ssid, const char* password) {
  // kickoff non-bloccante
  if (WiFi.status() == WL_CONNECTED) { s_wifiConnected = true; return true; }
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);
  WiFi.mode(WIFI_STA);

  static bool started = false;
  if (!started) {
    WiFi.disconnect(true, true);
    delay(10);
    WiFi.begin(ssid, password);
    started = true;
  }
  s_wifiConnected = (WiFi.status() == WL_CONNECTED);
  return s_wifiConnected;
}

bool wifiIsConnected() {
  return (WiFi.status() == WL_CONNECTED);
}

void wifiCamBeginUart(int rx, int tx, uint32_t baud, int uartNum) {
  s_camBaud    = baud;
  s_camUartNum = uartNum;
  if (!s_camLink) s_camLink = new HardwareSerial(uartNum);
  s_camLink->begin(baud, SERIAL_8N1, rx, tx);
}

// Protocollo CAM: "GET <name>.bin\n" -> "OK <len>\n" + <len bytes> + "DONE\n"
bool camFetchExact(const char* nameOrPath, uint8_t* dst, size_t len) {
  if (!s_camLink || !nameOrPath || !*nameOrPath || !dst || !len) return false;

  // lock: non sovrapporre a WHO
  uint32_t w0 = millis();
  while (s_camBusy && millis() - w0 < 200) delay(1);
  if (s_camBusy) return false;
  s_camBusy = true;
Faces_CamBusyBegin();   // <<< SYNC BUSY GUARD

  // nome pulito (.bin)
  String name = nameOrPath; name.trim();
  int slash = name.lastIndexOf('/');
  if (slash >= 0) name = name.substring(slash + 1);
  String path = sanitizeBinPath(name);
  if (path.isEmpty()) { 
  s_camBusy = false; 
  Faces_CamBusyEnd();      // <<< rilascia SEMPRE
  return false; 
}
  camLinkFlushRx(10);
  s_camLink->print("GET ");
  s_camLink->print(path.substring(1));     // senza '/'
  s_camLink->print("\n");

  // attendi header "OK <len>" (ignora eco/sporco)
  String header;
  bool gotHeader = false;
  uint32_t t0 = millis();
  while (millis() - t0 < CAM_UART_HEADER_TIMEOUT_MS) {
    if (readLine(*s_camLink, header, 80)) {
      if (header.startsWith("OK ")) { gotHeader = true; break; }
      if (header.startsWith("GET ") || header == "WHO") continue;   // eco
      if (header.startsWith("ERR")) { s_camBusy = false; return false; } // 404/PATH
      // qualsiasi altra riga inattesa: continua a cercare "OK "
    }
  }
    if (!gotHeader) { s_camBusy = false; Faces_CamBusyEnd(); return false; }

  uint32_t camLen = header.substring(3).toInt();
   if (camLen < (uint32_t)len) { s_camBusy = false; Faces_CamBusyEnd(); return false; }

  // leggi esattamente 'len' byte (fail se silenzio prolungato)
  size_t got = 0;
  t0 = millis();
  while (got < len) {
    int n = s_camLink->readBytes((char*)dst + got, len - got);
    if (n <= 0) {
      if (millis() - t0 > CAM_UART_SILENCE_TIMEOUT_MS) { s_camBusy=false; Faces_CamBusyEnd(); return false; }
      delay(1);
      continue;
    }
    got += (size_t)n;
    t0 = millis(); // reset watchdog di silenzio ad ogni progresso
  }

  // scarta eventuale extra
  uint32_t extra = camLen - (uint32_t)len;
  static uint8_t drop[512];
  while (extra) {
    size_t chunk = (extra > sizeof(drop)) ? sizeof(drop) : extra;
    size_t r = s_camLink->readBytes((char*)drop, chunk);
    if (r == 0) break;
    extra -= r;
  }

  // tail "DONE" (breve)
  String tail;
  bool ok = readLine(*s_camLink, tail, CAM_UART_TAIL_TIMEOUT_MS) && (tail == "DONE");
  s_camBusy = false;
  Faces_CamBusyEnd();     // <<< SYNC BUSY GUARD
  return ok;
}

// "WHO\n" -> "marco\n" / "none\n"
bool camWho(String& outName) {
  if (!s_camLink) return false;

  // lock: evita overlap con GET
  uint32_t w0 = millis();
  while (s_camBusy && millis() - w0 < 200) delay(1);
  if (s_camBusy) return false;
  s_camBusy = true;
   Faces_CamBusyBegin();    // <<<
   

  camLinkFlushRx(10);
  s_camLink->print("WHO\n");

  String line;
  bool ok = false;
  uint32_t t0 = millis();
  while (millis() - t0 < 220) {               // WHO max ~220 ms
    if (readLine(*s_camLink, line, 80)) {
      line.trim();
      if (line.length() == 0) continue;
      if (line == "WHO" || line.startsWith("GET ") || line.startsWith("OK ")) continue; // eco/sporco
      ok = true; break;
    }
  }
  s_camBusy = false;
  Faces_CamBusyEnd();      // <<<
  
  if (!ok) return false;
  outName = line;
  return true;
}

// Log non-bloccante sul cambio di stato Wi-Fi
void wifiCamPump(const char* ssid) {
  static wl_status_t prev = WL_NO_SHIELD;
  wl_status_t curr = WiFi.status();

  if (curr != prev) {
    prev = curr;
    if (curr == WL_CONNECTED) {
      USBSerial.printf("[WiFi] Connesso a %s, IP=%s RSSI=%d dBm\n",
                       ssid ? ssid : "(?)",
                       WiFi.localIP().toString().c_str(),
                       WiFi.RSSI());
    } else {
      USBSerial.println("[WiFi] NON connesso");
    }
  }

  // Se è giù ma abbiamo già fatto begin in setup, lasciamo che l’autoreconnect lavori.
  // Se vuoi forzare un tentativo ogni tanto, qui potresti richiamare begin()
  // con parsimonia (non più spesso di ogni 10-15s).
}
// Invia una riga verso la CAM sulla UART aperta con wifiCamBeginUart()
static inline void camSendLine(const char* s) {
  if (!s_camLink) return;
  s_camLink->print(s);
  s_camLink->print('\n');
}
// --- helper: leggi una riga \n dalla CAM con timeout ---
static bool camReadLine(String &line, uint32_t timeoutMs = 300) {
  if (!s_camLink) return false;
  return readLine(*s_camLink, line, timeoutMs);  // riusa l'helper già definito sopra
}

// Helper: invia comando e attende una risposta che inizi con 'startsWith'.
// Se 'replyOut' non è null, ci copia l'intera riga di risposta.
static bool camSendAndExpect(const char* cmd,
                             const char* startsWith,
                             String* replyOut = nullptr,
                             uint32_t timeoutMs = 400) {
  if (!s_camLink) return false;

  // lock per non sovrapporsi a GET/WHO
  uint32_t w0 = millis();
  while (s_camBusy && millis() - w0 < 200) delay(1);
  if (s_camBusy) return false;
  s_camBusy = true;
 Faces_CamBusyBegin();   // <<<
 
  camLinkFlushRx(10);
  camSendLine(cmd);

  String line;
  bool ok = false;
  uint32_t t0 = millis();

  // tollera eco o righe spurie finché non arriva quella giusta (o timeout)
  while (millis() - t0 < timeoutMs) {
    if (!camReadLine(line, timeoutMs)) break;
    if (line.length() == 0) continue;

    // ignora eventuale eco o rumore
    if (line == cmd || line.startsWith("GET ") || line == "WHO") continue;

    if (line.startsWith(startsWith)) { ok = true; break; }
    if (line.startsWith("ERR"))      { ok = false; break; }
    // altrimenti continua finché non troviamo la riga attesa
  }

  s_camBusy = false;
Faces_CamBusyEnd();     // <<<

  if (!ok) return false;
  if (replyOut) *replyOut = line;
  return true;
}

bool camEnrollSetName(const char* name) {
  if (!name || !*name) return false;
  String cmd = "ENROLL NAME ";
  cmd += name;
  // attende "ENROLL NAME OK"
  return camSendAndExpect(cmd.c_str(), "ENROLL NAME OK", nullptr, 600);
}

// ===== Streaming =====

bool camStreamOn() {
  // la CAM risponde "STREAM ON OK" se tutto ok
  return camSendAndExpect("STREAM ON", "STREAM ON");
}

bool camStreamOff() {
  // la CAM risponde "STREAM OFF OK"
  return camSendAndExpect("STREAM OFF", "STREAM OFF");
}

bool camStreamGetUrl(String &outUrl) {
  String line;
  // la CAM risponde "URL http://<ip>:81"
  if (!camSendAndExpect("STREAM URL?", "URL ", &line)) return false;
  outUrl = line.substring(4);  // togli "URL "
  outUrl.trim();
  USBSerial.printf("[CAM] stream URL: %s\n", outUrl.c_str());
  return outUrl.length() > 0;
}

// ===== Enroll =====
bool camEnrollOnce() {
  // la CAM risponde "ENROLL ONCE OK"
  return camSendAndExpect("ENROLL ONCE", "ENROLL ONCE");
}

bool camEnrollOn() {
  // la CAM risponde "ENROLL ON OK"
  return camSendAndExpect("ENROLL ON", "ENROLL ON");
}

bool camEnrollOff() {
  // la CAM risponde "ENROLL OFF OK"
  return camSendAndExpect("ENROLL OFF", "ENROLL OFF");
}



// ---- piccola util per parsare "http://x.x.x.x:81/"
static bool parseHttpUrl(const String& url, String& host, uint16_t& port) {
  host=""; port=80;
  String u=url; u.trim();
  if (u.startsWith("http://")) u.remove(0,7);
  int slash=u.indexOf('/');
  String hp=(slash>=0)?u.substring(0,slash):u;
  int colon=hp.indexOf(':');
  if (colon>=0){ host=hp.substring(0,colon); port=(uint16_t)hp.substring(colon+1).toInt(); }
  else host=hp;
  return host.length()>0;
}
// ---- scarica /jpeg in un buffer dinamico (PSRAM se disponibile)
// // --- Leggi un frame dal flusso MJPEG a un path ("/" o "/mjpeg")
static bool httpGetOneJpegFromMjpegPath(String host, uint16_t port, const char* path,
                                        uint8_t** outBuf, size_t* outLen,
                                        uint32_t timeoutMs = 2500) {
  *outBuf = nullptr; *outLen = 0;
  WiFiClient c;
  if (!c.connect(host.c_str(), port)) return false;

  // richiesta minimale (niente "Connection: close", alcuni server chiudono subito)
  c.print("GET "); c.print(path); c.print(" HTTP/1.1\r\nHost: ");
  c.print(host); c.print("\r\n\r\n");

  // salta gli header fino alla riga vuota
  uint32_t t0 = millis();
  while (millis() - t0 < 800) {
    String line = c.readStringUntil('\n'); line.trim();
    if (line.length() == 0) break;
  }

  const size_t MAX_FRAME = 250 * 1024;
  uint8_t* buf = (uint8_t*) heap_caps_malloc(MAX_FRAME, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!buf) return false;

  bool     inJpeg = false;
  size_t   wr = 0;
  uint8_t  prev = 0x00;
  t0 = millis();

  while (millis() - t0 < timeoutMs && c.connected()) {
    while (c.available()) {
      uint8_t b = c.read();

      if (!inJpeg) {
        if (prev == 0xFF && b == 0xD8) { inJpeg = true; wr = 0; buf[wr++] = 0xFF; buf[wr++] = 0xD8; }
        prev = b;
        continue;
      }

      if (wr < MAX_FRAME) buf[wr++] = b;

      if (prev == 0xFF && b == 0xD9) {      // fine JPEG
        *outBuf = buf; *outLen = wr;
        return true;
      }
      prev = b;
      t0 = millis();
    }
    delay(1);
  }

  free(buf);
  return false;
}


// ——— Prova varie rotte di still image; se falliscono, tenta l’estrazione dal MJPEG root
static bool httpGetAnyJpeg(String host, uint16_t port,
                           uint8_t** outBuf, size_t* outLen,
                           uint32_t timeoutMs = 1200) {
  *outBuf = nullptr; *outLen = 0;
  WiFiClient c;
  const char* paths[] = { "/jpeg", "/jpg", "/capture", "/snapshot.jpg" };

  for (size_t i = 0; i < sizeof(paths)/sizeof(paths[0]); ++i) {
    if (!c.connect(host.c_str(), port)) continue;

    c.setTimeout(2000);
    c.print("GET "); c.print(paths[i]); c.print(" HTTP/1.1\r\nHost: ");
    c.print(host); c.print("\r\nConnection: close\r\n\r\n");

    // Header
    int     contentLen   = -1;
    String  contentType  = "";
    uint32_t t0 = millis();
    while (millis() - t0 < 800) {
      String line = c.readStringUntil('\n'); line.trim();
      if (line.length() == 0) break;                 // fine header
      if (line.startsWith("Content-Length:")) {
        contentLen = line.substring(15).toInt();
      }
      else if (line.startsWith("Content-Type:")) {
        contentType = line.substring(13); contentType.trim();
      }
    }

    if (contentType.indexOf("image/jpeg") >= 0) {
      // Caso A: Content-Length presente -> alloc esatta
      if (contentLen > 0) {
        uint8_t *buf = (uint8_t*) heap_caps_malloc(contentLen, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!buf) { c.stop(); return false; }

        size_t got = 0; t0 = millis();
        while (got < (size_t)contentLen) {
          int n = c.read(buf + got, contentLen - got);
          if (n <= 0) { if (millis() - t0 > timeoutMs) { free(buf); break; } delay(1); continue; }
          got += (size_t)n; t0 = millis();
        }

        if (got >= 2 && buf[0] == 0xFF && buf[1] == 0xD8) {
          *outBuf = buf; *outLen = got;
          USBSerial.printf("[VIEW] path %s ok (CL=%d), %u bytes\n", paths[i], contentLen, (unsigned)got);
          return true;
        }
        free(buf);
      }
      // Caso B: NO Content-Length -> leggi fino a chiusura/timeout (capped)
      else {
        const size_t MAX_FRAME = 250 * 1024;
        uint8_t *buf = (uint8_t*) heap_caps_malloc(MAX_FRAME, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!buf) { c.stop(); return false; }

        size_t got = 0; t0 = millis();
        while (c.connected()) {
          int avail = c.available();
          if (avail <= 0) {
            if (millis() - t0 > timeoutMs) break;
            delay(1); continue;
          }
          int room = (int)(MAX_FRAME - got);
          if (room <= 0) break;  // pieno
          int n = c.read(buf + got, (avail < room) ? avail : room);
          if (n > 0) { got += (size_t)n; t0 = millis(); }
        }

        if (got >= 2 && buf[0] == 0xFF && buf[1] == 0xD8) {
          *outBuf = buf; *outLen = got;
          USBSerial.printf("[VIEW] path %s ok (no CL), %u bytes\n", paths[i], (unsigned)got);
          return true;
        }
        free(buf);
      }
    }

    c.stop();
  }

  // Fallback: leggi primo frame dal MJPEG su "/" e poi, se serve, da "/mjpeg"
  USBSerial.println("[VIEW] still fallback -> read first MJPEG frame from /");
  if (httpGetOneJpegFromMjpegPath(host, port, "/", outBuf, outLen, 2500)) return true;
if (httpGetOneJpegFromMjpegPath(host, port, "/mjpeg", outBuf, outLen, 2500)) return true;


  USBSerial.println("[VIEW] trying MJPEG at /mjpeg ...");
  

  USBSerial.println("[VIEW] MJPEG fallback FAIL");
  return false;
}



// ---- disegna un JPEG sul display (adatta alle dimensioni schermo)
static bool drawJpegToDisplay(uint8_t* jpg, size_t len, int x, int y, int maxW, int maxH) {
  if (JpegDec.decodeArray(jpg, len) != 1) return false;

  const int imgW = JpegDec.width;
  const int imgH = JpegDec.height;

  while (JpegDec.read()) {
    uint16_t *p   = JpegDec.pImage;   // blocco in RGB565
    int mcuW      = JpegDec.MCUWidth;
    int mcuH      = JpegDec.MCUHeight;
    int xd        = x + JpegDec.MCUx * mcuW;
    int yd        = y + JpegDec.MCUy * mcuH;
    int w         = ((xd + mcuW) > (x + maxW)) ? (x + maxW - xd) : mcuW;
    int h         = ((yd + mcuH) > (y + maxH)) ? (y + maxH - yd) : mcuH;
    if (w > 0 && h > 0) {
      gfx->draw16bitRGBBitmap(xd, yd, p, w, h);
      // se servisse byte-swap su questo display, usa draw16bitBeRGBBitmap(...)
    }
  }
  JpegDec.abort();
  return true;
}

// Disegna il JPEG applicando rotazione 0/90/180/270 usando la rotazione HW del display
static void drawJpegToDisplayRot(uint8_t* jpg, size_t len,
                                 int x, int y, int maxW, int maxH,
                                 uint8_t rot90)
{
  if (rot90 == 0) {
    drawJpegToDisplay(jpg, len, x, y, maxW, maxH);
    return;
  }

  // salviamo e cambiamo rotazione HW del display solo per questo draw
  uint8_t oldRot = gfx->getRotation();
  gfx->setRotation((oldRot + rot90) & 0x03);

  // dimensioni nello spazio ruotato
  int W = gfx->width();
  int H = gfx->height();

  // rimappa la posizione voluta (x,y,maxW,maxH) nel nuovo sistema ruotato
  int dx = x, dy = y, dw = maxW, dh = maxH;
  switch (rot90) {
    case 1: // 90° CW
      dx = H - (y + maxH);
      dy = x;
      dw = maxH;
      dh = maxW;
      break;
    case 2: // 180°
      dx = W - (x + maxW);
      dy = H - (y + maxH);
      // dw/dh invariati
      break;
    case 3: // 270° CW (o 90° CCW)
      dx = y;
      dy = W - (x + maxW);
      dw = maxH;
      dh = maxW;
      break;
  }

  drawJpegToDisplay(jpg, len, dx, dy, dw, dh);
  gfx->setRotation(oldRot);
}


// ---- API viewer ----
bool camViewStart() {
  if (s_viewOn) return true;
  // prova a farti dare l’URL dalla CAM
  String url;
  if (camStreamGetUrl(url) && parseHttpUrl(url, s_host, s_port)) {
    USBSerial.printf("[VIEW] source: http://%s:%u/jpeg\n", s_host.c_str(), s_port);
  } else {
    USBSerial.println("[VIEW] WARN: uso host/porta di default");
  }
  s_viewOn = true;
  camViewSetRotation(3 /*=270°*/);
  USBSerial.println("[VIEW] ON");
  return true;
}

void camViewStop() {
  if (!s_viewOn) return;
  s_viewOn = false;
  // 1) cancella l’area usata dal viewer
  if (gfx) gfx->fillRect(s_posX, s_posY, s_targetW, s_targetH, VIEW_CLEAR_COLOR);

  // 2) ripristina i canvas LVGL
  if (gEyesCanvas)  lv_obj_clear_flag(gEyesCanvas,  LV_OBJ_FLAG_HIDDEN);
  if (gMouthCanvas) lv_obj_clear_flag(gMouthCanvas, LV_OBJ_FLAG_HIDDEN);

  // 3) forza un redraw di LVGL (così copre subito eventuali residui)
  lv_obj_invalidate(lv_scr_act());
  // se vuoi refresh immediato (opzionale, se disponibile):
  // lv_refr_now(NULL);

  USBSerial.println("[VIEW] OFF + area cleared");
}

void camViewTick(uint32_t now) {
  if (!s_viewOn) return;
  if ((int32_t)(now - s_nextPullMs) < 0) return;
  s_nextPullMs = now + 160;  // ~6 fps

  uint8_t* jpg=nullptr; size_t len=0;
  if (!httpGetAnyJpeg(s_host, s_port, &jpg, &len, 900)) {
    USBSerial.println("[VIEW] fetch FAIL");
    return;
  }
  if (len < 2) { free(jpg); return; }

  USBSerial.printf("[VIEW] jpg=%u bytes magic=%02X %02X\n",
                   (unsigned)len, jpg[0], jpg[1]);

  // Se hai già la drawJpegToDisplay(...):
  drawJpegToDisplayRot(jpg, len, s_posX, s_posY, s_targetW, s_targetH, s_viewRot);


  free(jpg);
}









// Avvia enroll mode (sticky sulla CAM), stream OFF
bool Enroll_On_Display() {
  // lo stream disabilita il riconoscimento sulla CAM → spegnilo sempre
  (void)camStreamOff();
camViewStop();   
  // abilita enroll sticky sulla CAM
  if (!camSendAndExpect("ENROLL ON", "ENROLL ON", nullptr, 400)) {
    USBSerial.println("[ENROLL] CAM non ha confermato ENROLL ON");
    return false;
  }

  gEnroll.active = true;
  gEnroll.waitingUnknown = false;
  gEnroll.awaitingName   = false;
  gEnroll.lastWho = "none";
  gEnroll.lastPoll = 0;

  USBSerial.println("[ENROLL] modalità attiva. Avvicina un volto.");
  return true;
}

void Enroll_Off_Display() {
  (void)camSendAndExpect("ENROLL OFF", "ENROLL OFF", nullptr, 400);
  gEnroll = {};  // reset struct
  USBSerial.println("[ENROLL] modalità disattivata.");
}

// Chiamare spesso nel loop del display
void Enroll_Tick_Display(uint32_t nowMs) {
  if (!gEnroll.active) return;

  // rallenta le WHO a ~2 Hz
  if ((int32_t)(nowMs - (int32_t)gEnroll.lastPoll) < 500) return;
  gEnroll.lastPoll = nowMs;

  String whoRaw = "none";
  bool ok = camWho(whoRaw);
  if (!ok) return;

  String who = normWho(whoRaw);
  if (who == gEnroll.lastWho) return;
  gEnroll.lastWho = who;

  if (who == "none") {
    // nessuno di fronte: reset eventuale attesa
    if (gEnroll.waitingUnknown || gEnroll.awaitingName) {
      USBSerial.println("[ENROLL] volto perso, torna pure davanti alla camera.");
    }
    gEnroll.waitingUnknown = false;
    gEnroll.awaitingName   = false;
    return;
  }

  if (who == "unknown") {
    if (!gEnroll.awaitingName) {
      gEnroll.waitingUnknown = true;
      USBSerial.println("[ENROLL] volto sconosciuto rilevato.");
      USBSerial.println("  Vuoi salvarlo? Scrivi: yes  |  no");
    }
    return;
  }

  // è un nome noto
  USBSerial.printf("[ENROLL] volto riconosciuto: %s\n", who.c_str());
  // restiamo in modalità enroll finché l’utente non fa "enroll off"
}

// L’utente risponde “sì, salva”
void Enroll_SaveYes_Display() {
  if (!gEnroll.active) { USBSerial.println("[ENROLL] non attivo."); return; }
  if (!gEnroll.waitingUnknown) { USBSerial.println("[ENROLL] non vedo un volto 'unknown' al momento."); return; }
  gEnroll.awaitingName = true;
  USBSerial.println("[ENROLL] ok. Dimmi il nome:<nome>");
}

// L’utente risponde “no, non salvare”
void Enroll_SaveNo_Display() {
  if (!gEnroll.active) { USBSerial.println("[ENROLL] non attivo."); return; }
  gEnroll.waitingUnknown = false;
  gEnroll.awaitingName   = false;
  USBSerial.println("[ENROLL] non salvo. Resto in attesa di nuovi volti.");
}

// L’utente invia il nome
void Enroll_SendName_Display(const char* name) {
  if (!gEnroll.active) { USBSerial.println("[ENROLL] non attivo."); return; }
  if (!gEnroll.awaitingName) {
    USBSerial.println("[ENROLL] prima conferma con: enroll save yes");
    return;
  }
  if (!name || !*name) {
    USBSerial.println("[ENROLL] nome vuoto.");
    return;
  }

  // invia il nome alla CAM (non blocchiamoci sulla risposta)
  String cmd = String("ENROLL NAME ") + name;
(void)camSendAndExpect(cmd.c_str(), "ENROLL NAME OK", nullptr, 600);
  USBSerial.printf("[ENROLL] nome inviato alla CAM: %s\n", name);

  // ora la CAM farà la foto + recognition.enroll(name) appena vede ancora l’unknown
  gEnroll.awaitingName   = false;
  gEnroll.waitingUnknown = false;
  USBSerial.println("[ENROLL] tieni il volto davanti un attimo. Se va a buon fine comparirà il nome su WHO.");
}


bool wifiCamHandleCmd(const String& cmd, String* out) {
  USBSerial.printf("[WIFICAM CMD] '%s'\n", cmd.c_str());

  if (cmd == "cam stream on") {
  bool ok = camStreamOn();
  if (!ok) { if (out) *out = ""; return true; }

  String url;
  if (camStreamGetUrl(url)) {
    // manda SOLO url
    if (out) *out = url;          // <-- SOLO URL
  } else {
    if (out) *out = "http://192.168.4.1:81"; // fallback
  }
  return true;
}


  else if (cmd == "cam stream off") {
    USBSerial.println("[WIFICAM] match: cam stream off");
    bool ok = camStreamOff();
    if (out) *out = ok ? "STREAM OFF OK" : "STREAM ERR";
    return true;
  }

  else if (cmd == "cam stream url" || cmd == "cam url") {
    USBSerial.println("[WIFICAM] match: cam stream url");
    String url;
    bool ok = camStreamGetUrl(url);
    if (ok) {
      parseHttpUrl(url, s_host, s_port);
      USBSerial.printf("[VIEW] source: http://%s:%u/jpeg\n", s_host.c_str(), s_port);
    }
    if (out) *out = ok ? url : "STREAM URL ERR";
    return true;
  }

  else if (cmd == "cam view on") {
    USBSerial.println("[WIFICAM] match: cam view on");
    String url;
    if (camStreamGetUrl(url)) {
      parseHttpUrl(url, s_host, s_port);
      USBSerial.printf("[VIEW] source: http://%s:%u/jpeg\n", s_host.c_str(), s_port);
    }
    camViewStart();
    if (out) *out = "VIEW ON OK";
    return true;
  }

  else if (cmd == "cam view off") {
    USBSerial.println("[WIFICAM] match: cam view off");
    camViewStop();
    if (out) *out = "VIEW OFF OK";
    return true;
  }

  // ----- ENROLL (dopo stream/view) -----
  else if (cmd == "enroll on") {
    USBSerial.println("[WIFICAM] match: enroll on");
    if (Enroll_On_Display()) { if (out) *out = "ENROLL MODE ON"; }
    else                      { if (out) *out = "ENROLL MODE ERR"; }
    return true;
  }

  else if (cmd == "enroll off") {
    USBSerial.println("[WIFICAM] match: enroll off");
    Enroll_Off_Display();
    if (out) *out = "ENROLL MODE OFF";
    return true;
  }

  else if (cmd == "enroll save yes") {
    USBSerial.println("[WIFICAM] match: enroll save yes");
    Enroll_SaveYes_Display();
    if (out) *out = "ENROLL OK";
    return true;
  }

  else if (cmd == "enroll save no") {
    USBSerial.println("[WIFICAM] match: enroll save no");
    Enroll_SaveNo_Display();
    if (out) *out = "ENROLL OK";
    return true;
  }

  else if (cmd.startsWith("enroll name ")) {
    USBSerial.println("[WIFICAM] match: enroll name");
    String name = cmd.substring(12); name.trim();
    Enroll_SendName_Display(name.c_str());
    if (out) *out = "ENROLL OK";
    return true;
  }

  return false; // non gestito
}
