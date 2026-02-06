#ifndef Oscillator_h
#define Oscillator_h

#include <Arduino.h>

// Se non definito, definisco PI
#ifndef M_PI
  #define M_PI 3.14159265358979323846
#endif

// Converte gradi in radianti (usata da EMOtto.cpp)
#ifndef DEG2RAD
  #define DEG2RAD(g) ((g) * M_PI / 180.0)
#endif

class Oscillator {
public:
  // Costruttore: trim opzionale
  Oscillator(int trim = 0) 
  : _amplitude(45),
    _offset(0),
    _period(2000),
    _phase0(0),
    _pos(90),
    _trim(trim),
    _phase(0),
    _inc(0),
    _numberSamples(0),
    _samplingPeriod(30),
    _previousMillis(0),
    _currentMillis(0),
    _stop(false),
    _rev(false),
    _diff_limit(0),
    _previousServoCommandMillis(0),
    _pin(-1)
  {}

  // Collegamento al canale del PCA9685 (non al pin MCU!)
  void attach(int pin, bool rev = false);
  void detach();

  // Setters parametri oscillatore
  void SetA(unsigned int amplitude) { _amplitude = amplitude; }
  void SetO(int offset)             { _offset = offset; }
  void SetPh(double Ph)             { _phase0 = Ph; }
  void SetT(unsigned int period);

  // Trim e limiter
  void SetTrim(int trim)            { _trim = trim; }
  int  getTrim() const              { return _trim; }

  void SetLimiter(int diff_limit)   { _diff_limit = diff_limit; }
  void DisableLimiter()             { _diff_limit = 0; }

  // Stato / posizione
  void SetPosition(int position);   // imposta logica + invia subito
  int  getPosition() const          { return _pos; }

  // Controllo oscillazione
  void Stop()  { _stop = true; }
  void Play()  { _stop = false; }
  void Reset() { _phase = 0; }

  // Aggiornamento periodico sinusoidale
  // Ritorna l’angolo corrente
  int refresh();

  // Scrittura immediata (pubblica per compatibilità con codice esistente)
  void write(int position);

private:
  // timing per campionamento
  bool next_sample();

  // Parametri oscillatore
  unsigned int _amplitude;   // gradi
  int          _offset;      // gradi
  unsigned int _period;      // ms
  double       _phase0;      // radianti

  // Stato interno
  int          _pos;         // gradi correnti (logici, 0..180)
  int          _trim;        // correzione in gradi
  double       _phase;       // fase attuale
  double       _inc;         // incremento fase
  double       _numberSamples;
  unsigned int _samplingPeriod; // ms

  unsigned long _previousMillis;
  unsigned long _currentMillis;

  bool  _stop;     // se true, fermo in pos attuale
  bool  _rev;      // inversione cinematica
  int   _diff_limit;               // gradi/secondo (0 = disabled)
  unsigned long _previousServoCommandMillis;

  int   _pin;      // canale PCA9685 (0..15)
};

#endif
