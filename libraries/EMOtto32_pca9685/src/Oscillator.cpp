#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>
#include "Oscillator.h"

// =====================
// PCA9685 globale
// =====================
extern Adafruit_PWMServoDriver g_pca9685;

// =====================
// Taratura impulsi
// (adatta se i tuoi servi richiedono range diversi)
// =====================
#define SERVO_MIN 172    // ~0°
#define SERVO_MAX 565    // ~180°

static inline int angleToPWM(int ang) {
  if (ang < 0)   ang = 0;
  if (ang > 180) ang = 180;
  return map(ang, 0, 180, SERVO_MIN, SERVO_MAX);
}

// =====================
// Timing campionamento
// =====================
bool Oscillator::next_sample() {
  _currentMillis = millis();
  if (_currentMillis - _previousMillis > _samplingPeriod) {
    _previousMillis = _currentMillis;
    return true;
  }
  return false;
}

// =====================
// attach/detach
// =====================
void Oscillator::attach(int pin, bool rev) {
  _pin = pin;        // è il canale del PCA9685 (0..15)
  _rev = rev;

  // Default iniziali
  _samplingPeriod = 30;
  _period = 2000;
  _numberSamples = (double)_period / _samplingPeriod;
  _inc = 2.0 * M_PI / _numberSamples;

  _phase = 0;
  _phase0 = 0;
  _offset = 0;
  _amplitude = 45;
  _stop = false;

  _pos = 90;
  _previousServoCommandMillis = millis();

  // Porta subito a 90° reali (con trim)
  write(90);
}

void Oscillator::detach() {
  // Con il PCA9685 non c’è un vero “detach”.
  // Per sicurezza, non inviamo più niente finché non si richiama attach().
  _pin = -1;
}

// =====================
// Parametri periodo
// =====================
void Oscillator::SetT(unsigned int period) {
  _period = period;
  _numberSamples = (double)_period / _samplingPeriod;
  _inc = 2.0 * M_PI / _numberSamples;
}

// =====================
// SetPosition (logica + invio)
// =====================
void Oscillator::SetPosition(int position) {
  if (position < 0)   position = 0;
  if (position > 180) position = 180;
  write(position);
}

// =====================
// refresh sinusoidale
// =====================
int Oscillator::refresh() {
  if (!next_sample()) return _pos;

  if (!_stop) {
    // calcola oscillazione
    int pos = (int)round(_amplitude * sin(_phase + _phase0) + _offset);
    if (_rev) pos = -pos;
    pos += 90; // centro
    // limita ai 0..180
    if (pos < 0)   pos = 0;
    if (pos > 180) pos = 180;

    write(pos);
  }

  // avanza fase (anche se fermo, per coerenza temporale)
  _phase += _inc;
  if (_phase > 2.0 * M_PI) _phase -= 2.0 * M_PI;

  return _pos;
}

// =====================
// write() immediato verso PCA9685
// =====================
void Oscillator::write(int position) {
  // protezione bounds
  if (position < 0)   position = 0;
  if (position > 180) position = 180;

  unsigned long now = millis();

  // Limiter di delta (gradi/secondo)
  int target = position;
  if (_diff_limit > 0) {
    int dt = (int)(now - _previousServoCommandMillis); // ms
    // limite massimo consentito in questo intervallo
    int max_step = max(1, (dt * _diff_limit) / 1000);
    int diff = target - _pos;
    if (abs(diff) > max_step) {
      target = _pos + (diff > 0 ? max_step : -max_step);
    }
  }

  _pos = target;
  _previousServoCommandMillis = now;

  // Applica trim alla posizione finale *prima* di convertire in PWM
  int hw_angle = _pos + _trim;
  if (hw_angle < 0)   hw_angle = 0;
  if (hw_angle > 180) hw_angle = 180;

  int pulse = angleToPWM(hw_angle);

  // Se non “attached”, esco
  if (_pin < 0) return;

  // Scrivi sul canale del PCA9685
  g_pca9685.setPWM(_pin, 0, pulse);
}
