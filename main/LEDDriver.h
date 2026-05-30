// --- LEDDriver.h ---
#pragma once
#include <Arduino.h> // Required so the compiler understands digitalWrite, HIGH, LOW, etc.

class LEDDriver {
  private:
    // HARDCODED PIN ASSIGNMENTS
    const uint8_t pinMute = 39;
    const uint8_t pinRed = 40;
    const uint8_t pinIR = 42;

    // timing variables
    unsigned long lastMicros;
    unsigned long currentDelay;
    uint8_t phaseStep;

  public:
    // TIMING CONSTANTS
    unsigned long settleTimeUS = 2000; 
    unsigned long readTimeUS = 500;    

    LEDDriver() {
      lastMicros = 0;
      currentDelay = 0;
      phaseStep = 0;
    }

    void begin() {
      pinMatrixOutDetach(pinMute, false, false);
      pinMatrixOutDetach(pinRed, false, false);
      pinMatrixOutDetach(pinIR, false, false);

      pinMode(pinMute, OUTPUT);
      pinMode(pinRed, OUTPUT);
      pinMode(pinIR, OUTPUT);

      turnAllOff();
      lastMicros = micros();
    }

    void update() {
      unsigned long currentMicros = micros();
      
      if (currentMicros - lastMicros < currentDelay) {
        return; 
      }

      lastMicros = currentMicros;

      switch (phaseStep) {
        case 0: 
          turnRedOn(); 
          currentDelay = settleTimeUS; 
          phaseStep = 1;
          break;
        case 1: 
          currentDelay = readTimeUS; 
          phaseStep = 2;
          break;
        case 2: 
          turnAllOff(); 
          currentDelay = settleTimeUS; 
          phaseStep = 3;
          break;
        case 3: 
          currentDelay = readTimeUS; 
          phaseStep = 4;
          break;
        case 4: 
          turnIROn(); 
          currentDelay = settleTimeUS; 
          phaseStep = 5;
          break;
        case 5: 
          currentDelay = readTimeUS; 
          phaseStep = 6;
          break;
        case 6: 
          turnAllOff(); 
          currentDelay = settleTimeUS; 
          phaseStep = 7;
          break;
        case 7: 
          currentDelay = readTimeUS; 
          phaseStep = 0; 
          break;
      }
    }

    void turnRedOn() {
      digitalWrite(pinMute, LOW);   
      digitalWrite(pinIR, LOW);     
      digitalWrite(pinRed, HIGH);   
    }

    void turnIROn() {
      digitalWrite(pinMute, LOW);   
      digitalWrite(pinRed, LOW);    
      digitalWrite(pinIR, HIGH);    
    }

    void turnAllOff() {
      digitalWrite(pinMute, HIGH);  
      digitalWrite(pinRed, LOW);
      digitalWrite(pinIR, LOW);
    }
};