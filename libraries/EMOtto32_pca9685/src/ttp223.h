#pragma once
#include <Arduino.h>

// Gesture callback
struct TtpEvent {
  enum Type : uint8_t { Tap, DoubleTap, LongPress } type;
};

class TTP223 {
public:
  TTP223(int pin, bool activeHigh = true)
  : _pin(pin), _activeHigh(activeHigh) {}

  void begin(bool usePull = false) {
    if (usePull) pinMode(_pin, _activeHigh ? INPUT_PULLDOWN : INPUT_PULLUP);
    else         pinMode(_pin, INPUT);
    _stable = readRaw();
    _lastStable = _stable;
    _lastChangeMs = millis();
  }

  // chiama spesso (ogni loop); se c’è evento ritorna true e compila ev
  bool update(uint32_t now, TtpEvent &ev) {
    const bool raw = readRaw();

    // debounce: conferma cambi stato dopo DEBOUNCE_MS
    if (raw != _stable) {
      _stable = raw;
      _lastChangeMs = now;
    }
    if ((now - _lastChangeMs) < DEBOUNCE_MS) return false;

    // stato “debounced”
    const bool st = _stable;

    // edge: press
    if (st && !_lastStable) {
      _pressMs = now;
      _longFired = false;
    }

    // long press mentre premuto
    if (st && !_longFired && _pressMs && (now - _pressMs) >= LONG_MS) {
      _longFired = true;
      _tapCount = 0;         // annulla tap/doppio tap
      ev.type = TtpEvent::LongPress;
      _lastStable = st;
      return true;
    }

    // edge: release
    if (!st && _lastStable) {
      const uint32_t held = (now - _pressMs);
      _pressMs = 0;

      // se era long già emesso, ignora il rilascio
      if (_longFired) {
        _lastStable = st;
        return false;
      }

      // tap candidato (rilascio veloce)
      if (held <= TAP_MAX_MS) {
        _tapCount++;
        if (_tapCount == 1) {
          _tapDeadlineMs = now + DOUBLE_MS;
        } else if (_tapCount == 2) {
          _tapCount = 0;
          _tapDeadlineMs = 0;
          ev.type = TtpEvent::DoubleTap;
          _lastStable = st;
          return true;
        }
      } else {
        // press troppo lungo ma sotto LONG_MS: trattalo come tap singolo (opzionale)
        _tapCount = 1;
        _tapDeadlineMs = now + DOUBLE_MS;
      }
    }

    // scaduta finestra doppio tap => emetti Tap
    if (_tapCount == 1 && _tapDeadlineMs && (int32_t)(now - _tapDeadlineMs) >= 0) {
      _tapCount = 0;
      _tapDeadlineMs = 0;
      ev.type = TtpEvent::Tap;
      _lastStable = st;
      return true;
    }

    _lastStable = st;
    return false;
  }

  bool isPressed() const { return _lastStable; }

private:
  int  _pin;
  bool _activeHigh;

  // debounce & state
  bool _stable = false;
  bool _lastStable = false;
  uint32_t _lastChangeMs = 0;

  // gesture timing
  uint32_t _pressMs = 0;
  bool _longFired = false;

  uint8_t  _tapCount = 0;
  uint32_t _tapDeadlineMs = 0;

  static constexpr uint16_t DEBOUNCE_MS = 25;
  static constexpr uint16_t LONG_MS     = 550;
  static constexpr uint16_t DOUBLE_MS   = 320;
  static constexpr uint16_t TAP_MAX_MS  = 300;

  bool readRaw() const {
    const int v = digitalRead(_pin);
    return _activeHigh ? (v == HIGH) : (v == LOW);
  }
};
