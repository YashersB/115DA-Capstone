#pragma once
#include <Arduino.h> 

class LEDDriver {
  private:
    // UPDATED HARDCODED PIN ASSIGNMENTS
    const uint8_t pinMute = 39;
    const uint8_t pinRed = 41; // Changed to GPIO 41
    const uint8_t pinIR = 42;  // GPIO 42

    // timing variables
    unsigned long lastMicros;
    unsigned long currentDelay;
    uint8_t phaseStep;

  public:
    // --- TESTING TIMING CONSTANTS ---
    // Slowed down to 0.5 seconds so your eyes can see the flashes.
    // When you are ready for medical sampling, change these back to:
    // settleTimeUS = 2000;
    // readTimeUS = 500;
    unsigned long settleTimeUS = 500000; 
    unsigned long readTimeUS = 500000;    

    LEDDriver() {
      lastMicros = 0;
      currentDelay = 0;
      phaseStep = 0;
    }

    void begin() {
      pinMode(pinMute, OUTPUT);
      pinMode(pinRed, OUTPUT);
      pinMode(pinIR, OUTPUT);

      turnAllOff();
      lastMicros = micros();
    }

    // This MUST be called as fast as possible in the main loop
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