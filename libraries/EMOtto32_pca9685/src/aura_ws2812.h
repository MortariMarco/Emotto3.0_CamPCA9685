#pragma once
#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

// Usa la tua enum
// enum class ExprKind : uint8_t { Natural, Angry, Fear, Greeting, Sadness, Embarrassment, Disgust, Anxiety, Boredom, Sleep, Wakeup, Love, Avoid, Dance, Sing, Run, Yawn };

struct AuraState {
  uint8_t  brightness = 80;     // 0..255
  uint16_t nLeds      = 8; // numero di led

  // stato interno
  ExprKind lastExpr   = (ExprKind)255;
  uint32_t lastTickMs = 0;
  uint16_t phase      = 0;
  bool     flip       = false;
};

class AuraWS2812 {
public:
  AuraWS2812(uint8_t pin, uint16_t nLeds)
  : _strip(nLeds, pin, NEO_GRB + NEO_KHZ800) {
    _st.nLeds = nLeds;
  }

  void begin(uint8_t brightness = 80) {
    _st.brightness = brightness;
    _strip.begin();
    _strip.setBrightness(_st.brightness);
    _strip.show(); // off
  }

  // Chiama SEMPRE in loop: se expr cambia, switcha automaticamente effetto
  void update(uint32_t nowMs, ExprKind expr) {
    if (expr != _st.lastExpr) {
      _st.lastExpr = expr;
      _st.phase = 0;
      _st.flip = false;
      _st.lastTickMs = 0;
      // opzionale: spegni subito per evitare "coda" vecchio effetto
      // clear();
    }

    // refresh rate tipico: 20-50ms
    const uint32_t dt = nowMs - _st.lastTickMs;
    if (dt < 20) return;
    _st.lastTickMs = nowMs;

    render(expr, nowMs);
    _strip.show();
  }

  void setBrightness(uint8_t b) {
    _st.brightness = b;
    _strip.setBrightness(b);
  }

  void clear() {
    _strip.clear();
    _strip.show();
  }

private:
  Adafruit_NeoPixel _strip;
  AuraState _st;

  // --------- effetti base ----------
  void fill(uint32_t c) {
    for (uint16_t i=0; i<_st.nLeds; i++) _strip.setPixelColor(i, c);
  }

  // pulse morbido (sinusoidale approx con triangle)
  void pulse(uint32_t baseColor, uint8_t minB, uint8_t maxB, uint8_t speed) {
    _st.phase = (_st.phase + speed) & 255;
    uint8_t t = _st.phase;                 // 0..255
    uint8_t tri = (t < 128) ? (t*2) : (255 - (t-128)*2); // 0..255..0
    uint8_t b = map(tri, 0, 255, minB, maxB);

    // applica "b" come brightness locale: scala il colore
    uint8_t r = (uint8_t)((uint32_t)((baseColor >> 16) & 0xFF) * b / 255);
    uint8_t g = (uint8_t)((uint32_t)((baseColor >>  8) & 0xFF) * b / 255);
    uint8_t bl= (uint8_t)((uint32_t)((baseColor      ) & 0xFF) * b / 255);
    fill(_strip.Color(r,g,bl));
  }

  void blink(uint32_t c1, uint32_t c2, uint8_t speed) {
    _st.phase = (_st.phase + speed) & 255;
    if (_st.phase < 128) fill(c1); else fill(c2);
  }

  void chase(uint32_t c, uint8_t tailLen, uint8_t speed) {
    _st.phase = (_st.phase + speed) & 255;
    uint16_t head = (uint16_t)((uint32_t)_st.phase * _st.nLeds / 256);
    _strip.clear();
    for (uint8_t k=0; k<tailLen; k++) {
      int idx = (int)head - (int)k;
      while (idx < 0) idx += _st.nLeds;
      uint8_t dim = map(k, 0, tailLen-1, 255, 40);
      uint8_t r = (uint8_t)((uint32_t)((c >> 16) & 0xFF) * dim / 255);
      uint8_t g = (uint8_t)((uint32_t)((c >>  8) & 0xFF) * dim / 255);
      uint8_t b = (uint8_t)((uint32_t)((c      ) & 0xFF) * dim / 255);
      _strip.setPixelColor(idx % _st.nLeds, _strip.Color(r,g,b));
    }
  }

  uint32_t wheel(uint8_t p) {
    p = 255 - p;
    if (p < 85)  return _strip.Color(255 - p * 3, 0, p * 3);
    if (p < 170) { p -= 85; return _strip.Color(0, p * 3, 255 - p * 3); }
    p -= 170;    return _strip.Color(p * 3, 255 - p * 3, 0);
  }

  void rainbow(uint8_t speed) {
    _st.phase = (_st.phase + speed) & 255;
    for (uint16_t i=0; i<_st.nLeds; i++) {
      uint8_t p = (uint8_t)((i * 256 / _st.nLeds) + _st.phase);
      _strip.setPixelColor(i, wheel(p));
    }
  }

  // --------- mapping emozioni -> effetto ----------
  void render(ExprKind expr, uint32_t /*nowMs*/) {
    switch (expr) {
      case ExprKind::Natural:
  pulse(_strip.Color(0, 180, 0), 20, 160, 2);  // respiro verde
  break;
 
                 
      case ExprKind::Greeting: rainbow(1); break;
                                   // arcobaleno soft
      case ExprKind::Love:         pulse(_strip.Color(255, 0, 40), 10, 200, 5); break;  // “cuore”
      case ExprKind::Angry:        blink(_strip.Color(255, 0, 0), _strip.Color(0,0,0), 20); break;
      case ExprKind::Fear:         blink(_strip.Color(120, 0, 255), _strip.Color(0,0,0), 10); break;
      case ExprKind::Sadness:      pulse(_strip.Color(0, 0, 255), 5, 90, 2); break;
      case ExprKind::Anxiety: chase(_strip.Color(255, 80, 0), 3, 10); break;   // arancio “nervoso”
      case ExprKind::Boredom:      pulse(_strip.Color(60, 60, 60), 2, 40, 1); break;    // grigio lento
      case ExprKind::Sleep:        pulse(_strip.Color(0, 0, 120), 0, 50, 1); break;     // blu respiro
      case ExprKind::Wakeup:  chase(_strip.Color(0, 200, 255), 4, 28); break; // “accensione”
      case ExprKind::Disgust:      fill(_strip.Color(0, 180, 0)); break;                // verde
      case ExprKind::Embarrassment:pulse(_strip.Color(255, 0, 120), 5, 120, 3); break;  // fucsia
      case ExprKind::Avoid:        blink(_strip.Color(0, 0, 0), _strip.Color(30,30,30), 8); break;
      case ExprKind::Dance: rainbow(4); break;
      case ExprKind::Sing:    chase(_strip.Color(255, 255, 0), 4, 16); break;// giallo “stage”
      case ExprKind::Run:     chase(_strip.Color(0, 255, 0), 3, 18); break; 
	  case ExprKind::Yawn:         pulse(_strip.Color(200, 200, 255), 0, 60, 1); break;
      default:                     fill(_strip.Color(0,0,0)); break;
    }
  }
};
