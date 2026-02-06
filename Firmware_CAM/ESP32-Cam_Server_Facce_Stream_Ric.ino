//ver uart 9.8
// server Ap per distribuire i voltri del robot salvati su SD
// Rilevamento Volti
// Riconoscimento Volti
// Distribuisce i nomi dei volti conosciuti Via AP con server.on /who
// Funzione salvataggio nuovi volti richiamabile da http o app con server.on /enroll
// straming mpeg hhtp
// === ESP32-S3 CAM: AP + Streaming MJPEG + Face Detect/Recognize + UART bridge ===
// - WHO via UART -> "none" / "unknown" / "<nome>"
// - Enroll SOLO su comando UART: ENROLL ONCE/ON -> (alla prima faccia sconosciuta) ENROLL NAME?
//   poi rispondi con: ENROLL NAME <nome>
// - Durante lo streaming MJPEG non si fanno capture/detect per non degradare FPS

#include <WiFi.h>
#include <WebServer.h>
#include <eloquent_esp32cam.h>
#include <eloquent_esp32cam/face/detection.h>
#include <eloquent_esp32cam/face/recognition.h>
#include <eloquent_esp32cam/extra/esp32/fs/sdmmc.h>
#include <SD_MMC.h>
#include <eloquent_esp32cam/viz/mjpeg.h>

using namespace eloq;
using eloq::camera;
using eloq::face::detection;
using eloq::face::recognition;
using namespace eloq::viz;

// =================== Stato riconoscimento / enroll ===================
bool   enrollNextFace   = false;     // armato da ENROLL ONCE / ENROLL ON
bool   enrollSticky     = false;     // ON: resta attivo; ONCE: si spegne dopo 1 enroll
bool   lastDetected     = false;     // stato precedente di "riconosciuto"
String lastRecognized   = "none";    // "none" / "unknown" / "<nome>"
static String enrollName = "";       // nome ricevuto dal display con ENROLL NAME <nome>

// =================== Streaming MJPEG (porta 81) ===================
static bool gStreamOn = false;

static void startStream() {
  if (gStreamOn) return;
  while (!mjpeg.begin().isOk()) {
    Serial.println(mjpeg.exception.toString());
    delay(100);
  }
  gStreamOn = true;
  Serial.print("[STREAM] ON: http://");
  Serial.print(WiFi.softAPIP());
  Serial.println(":81/");
}

static void stopStream() {
  if (!gStreamOn) return;
  // Se la tua versione supporta lo stop esplicito: mjpeg.end();
  gStreamOn = false;
  Serial.println("[STREAM] OFF");
}

// =================== AP ===================
#define AP_SSID "OttoCamServer"
#define AP_PASS "12345678"

WebServer server(80);

// =================== UART LINK con display ===================
#define LINK_UART 1
#define LINK_RX   1   // scegli GPIO adatti per la tua board (NO 40: SD D0)
#define LINK_TX   2
#define LINK_BAUD 2000000
HardwareSerial Link(LINK_UART);

// ======= Prototipi =======
String prompt(String message);   // lasciata intatta (USB), ma NON usata nel flusso UART
void   enrollFace();             // lasciata intatta (USB), ma NON usata nel flusso UART
void   handleImage();            // placeholder

// ===================== UART helpers =====================
static bool readLine(HardwareSerial &ser, String &line, uint32_t timeoutMs = 100) {
  line = "";
  uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    while (ser.available()) {
      int c = ser.read();
      if (c < 0) break;
      if (c == '\n') { line.trim(); return true; }
      line += (char)c;
    }
    delay(1);
  }
  return false;
}

// Accetta "faccia_x.bin" o "/faccia_x.bin"; rifiuta http/https; deve finire in .bin
static String sanitizeBinPath(const String &inRaw) {
  String p = inRaw; p.trim();
  if (p.isEmpty()) return String();
  if (p.indexOf(' ') >= 0 || p.indexOf('\\') >= 0) return String();
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

// Invia file .bin dalla SD via UART: "OK <len>\n" + <bytes> + "DONE\n"
static void uartServeFile(const String &rawName) {
  String path = sanitizeBinPath(rawName);
  if (path.isEmpty()) { Link.println(F("ERR PATH")); return; }
  File file = SD_MMC.open(path, FILE_READ);
  if (!file) { Link.println(F("ERR 404")); return; }

  uint32_t len = file.size();
  Link.print(F("OK ")); Link.println(len);

  static uint8_t buf[4096];
  uint32_t left = len;
  while (left) {
    size_t n = file.read(buf, (left > sizeof(buf)) ? sizeof(buf) : left);
    if (n == 0) break;
    Link.write(buf, n);
    left -= n;
  }
  file.close();
  Link.println(F("DONE"));
}

// Risposta a WHO (ultimo stato consolidato)
static void uartServeWho() {
  Link.println(lastRecognized);   // "none" / "unknown" / "<nome>"
}

// (Facoltativo) trigger veloce per accodare un enroll
static void uartEnrollTrigger() {
  enrollNextFace = true;
  enrollSticky   = false;
  Link.println(F("ENROLL OK"));
}

// =================== Setup ===================
void setup() {
  delay(3000);
  Serial.begin(115200);
  Serial.println("___ESP32S3 FACE RECOGNITION + SD SERVER___");

  // UART link
  Link.begin(LINK_BAUD, SERIAL_8N1, LINK_RX, LINK_TX);
  Serial.println("[UART] pronto");

  // Camera
  camera.pinout.eye_s3();
  camera.brownout.disable();
  camera.resolution.face();
  camera.quality.high();

  // SD
  sdmmc.pinout.clk(39);
  sdmmc.pinout.cmd(38);
  sdmmc.pinout.d0(40);

  // Face
  detection.accurate();
  detection.confidence(0.7);
  recognition.confidence(0.85);

  // Start subsystems
  while (!camera.begin().isOk())       Serial.println(camera.exception.toString());
  while (!recognition.begin().isOk())  Serial.println(recognition.exception.toString());
  while (!sdmmc.begin().isOk())        Serial.println(sdmmc.exception.toString());

  // SD_MMC classica
  // if (!SD_MMC.begin("/sdcard", true)) {
  //   Serial.println("Card Mount Failed");
  //   return;
  // }

  // WiFi AP
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.print("AP started. IP: ");
  Serial.println(WiFi.softAPIP());

  // HTTP endpoints
  server.on("/stream/on",  HTTP_GET, [](){ startStream(); server.send(200, "text/plain", "OK"); });
  server.on("/stream/off", HTTP_GET, [](){ stopStream();  server.send(200, "text/plain", "OK"); });
  server.on("/stream/url", HTTP_GET, [](){
    String url = "http://" + WiFi.softAPIP().toString() + ":81/";
    server.send(200, "text/plain", url);
  });
  server.on("/who", HTTP_GET, [](){ server.send(200, "text/plain", lastRecognized); });

  server.begin();
  Serial.println("HTTP server pronto");
  Serial.println("Camera OK, SD OK, Face Recognition OK");
}

// =================== Loop ===================
void loop() {
  server.handleClient();

  // --- UART commands dal display ---
  if (Link.available()) {
    String line;
    if (readLine(Link, line, 5)) {
      if (line.startsWith("GET ")) {
        String name = line.substring(4);
        uartServeFile(name);
      }
      else if (line == "WHO") {
        uartServeWho();
      }
      else if (line == "STREAM ON") {
        if (!gStreamOn) {
          if (mjpeg.begin().isOk()) { gStreamOn = true; Link.println(F("STREAM ON OK")); }
          else                      { Link.println(F("STREAM ERR")); }
        } else {
          Link.println(F("STREAM ON OK"));
        }
      }
      else if (line == "STREAM OFF") {
        // se supportato: mjpeg.end();
        gStreamOn = false;
        Link.println(F("STREAM OFF OK"));
      }
      else if (line == "STREAM URL?") {
        IPAddress ip = WiFi.softAPIP();
        Link.print(F("URL http://")); Link.print(ip); Link.println(F(":81"));
      }
      else if (line == "ENROLL ONCE") {
        enrollNextFace = true; enrollSticky = false; Link.println(F("ENROLL ONCE OK"));
      }
      else if (line == "ENROLL ON") {
        enrollNextFace = true; enrollSticky = true;  Link.println(F("ENROLL ON OK"));
      }
      else if (line == "ENROLL OFF") {
        enrollNextFace = false; enrollSticky = false; enrollName = ""; Link.println(F("ENROLL OFF OK"));
      }
      else if (line.startsWith("ENROLL NAME ")) {
        String name = line.substring(12); name.trim();
        if (name.length() == 0) Link.println(F("ENROLL NAME ERR"));
        else { enrollName = name; Link.println(F("ENROLL NAME OK")); }
      }
      else {
        Link.println(F("ERR CMD"));
      }
    }
  }

  // === Se lo streaming è attivo, non catturare/riconoscere per non degradare FPS ===
  if (gStreamOn) {
    delay(2);
    return;
  }

  // === Face detect/recognize ===
  bool facePresent     = false;
  bool recognized      = false;
  String recognizedName = "none";

  if (camera.capture().isOk() && recognition.detect().isOk()) {
    facePresent = true;
    if (recognition.recognize().isOk() && recognition.match.name.length() > 0) {
      recognized     = true;
      recognizedName = recognition.match.name;
    }
  }

  // Aggiorna lastRecognized secondo la semantica richiesta
  String newWho;
  if (!facePresent)           newWho = "none";
  else if (!recognized)       newWho = "unknown";
  else                        newWho = recognizedName;

  if (newWho != lastRecognized) {
    lastRecognized = newWho;
    if (recognized) {
      Serial.printf(">>> Volto riconosciuto: %s (%.2f)\n",
                    recognition.match.name.c_str(),
                    recognition.match.similarity);
    } else if (facePresent) {
      Serial.println("Volto presente ma sconosciuto (unknown)");
    } else {
      Serial.println("Nessun volto (none)");
    }
  }

  lastDetected = recognized;

  // === Enroll SOLO su richiesta e SOLO se volto presente ma non riconosciuto ===
  if (enrollNextFace && facePresent && !recognized) {
    if (enrollName.length() == 0) {
      // chiedi il nome al display (rispondere ENROLL NAME <nome>)
      Link.println(F("ENROLL NAME?"));
    } else {
      Serial.printf("Enroll volto come '%s'...\n", enrollName.c_str());

      // salva foto corrente su SD
      String filename = "/" + enrollName + ".jpg";
      if (sdmmc.save(camera.frame).to(filename).isOk())
        Serial.printf("Foto salvata: %s\n", filename.c_str());
      else
        Serial.println(sdmmc.session.exception.toString());

      // aggiungi ai volti noti
      if (recognition.enroll(enrollName).isOk()) {
        Serial.println("Volto aggiunto!");
        Link.println(F("ENROLL OK"));
      } else {
        Serial.println(recognition.exception.toString());
        Link.println(F("ENROLL ERR"));
      }

      enrollName = "";
      if (!enrollSticky) enrollNextFace = false;  // ENROLL ONCE: disarma
    }
  }

  delay(10);
}

// ===== Stub HTTP (non usato qui) =====
void handleImage() {}

// ===== Prompt su USB (lasciata intatta, NON usata nel flusso UART) =====
String prompt(String message) {
  String answer;
  do {
    Serial.println(message);
    while (!Serial.available()) delay(1);
    answer = Serial.readStringUntil('\n');
    answer.trim();
  } while (!answer);
  return answer;
}

// ===== Enroll da USB (lasciata intatta per compatibilità con l'esempio) =====
void enrollFace() {
  String name = prompt("Nome persona:");
  if (name.length() == 0) return;

  String filename = "/" + name + ".jpg";
  if (sdmmc.save(camera.frame).to(filename).isOk())
    Serial.printf("Foto salvata: %s\n", filename.c_str());
  else
    Serial.println(sdmmc.session.exception.toString());

  if (recognition.enroll(name).isOk())
    Serial.println("Volto aggiunto!");
  else
    Serial.println(recognition.exception.toString());
}

