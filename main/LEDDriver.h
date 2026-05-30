// --- LEDDriver.h ---
#pragma once

#include <Arduino.h>

class LEDDriver {
  private:
    // Based on the pin mapping notes you shared from the schematic review.
    const uint8_t pinMute = 42;
    const uint8_t pinRed = 40;
    const uint8_t pinIR = 41;

    unsigned long lastMicros;
    unsigned long currentDelay;
    uint8_t phaseStep;

  public:
    unsigned long settleTimeUS = 2000;
    unsigned long readTimeUS = 500;

    LEDDriver() {
      lastMicros = 0;
      currentDelay = 0;
      phaseStep = 0;
    }

    uint8_t getPhase() const {
      return phaseStep;
    }

    void begin() {
      pinMatrixOutDetach(pinMute, false, false);
      pinMatrixOutDetach(pinRed, false, false);
      pinMatrixOutDetach(pinIR, false, false);

      pinMode(pinMute, OUTPUT);
      pinMode(pinRed, OUTPUT);
      pinMode(pinIR, OUTPUT);

      resetCycle();
    }

    void resetCycle() {
      turnRedOn();
      phaseStep = 0;
      currentDelay = settleTimeUS;
      lastMicros = micros();
    }

    void update() {
      unsigned long currentMicros = micros();

      if (currentMicros - lastMicros < currentDelay) {
        return;
      }

      lastMicros = currentMicros;

      // The delay for the CURRENT phase just finished.
      // Transition to the NEXT phase.
      uint8_t nextPhase = (phaseStep + 1) % 8;

      switch (nextPhase) {
        case 0: // Start Red Settle
          turnRedOn();
          currentDelay = settleTimeUS;
          break;
        case 1: // Start Red Read
          currentDelay = readTimeUS;
          break;
        case 2: // Start Amb1 Settle
          turnAllOff();
          currentDelay = settleTimeUS;
          break;
        case 3: // Start Amb1 Read
          currentDelay = readTimeUS;
          break;
        case 4: // Start IR Settle
          turnIROn();
          currentDelay = settleTimeUS;
          break;
        case 5: // Start IR Read
          currentDelay = readTimeUS;
          break;
        case 6: // Start Amb2 Settle
          turnAllOff();
          currentDelay = settleTimeUS;
          break;
        case 7: // Start Amb2 Read
          currentDelay = readTimeUS;
          break;
      }
      
      phaseStep = nextPhase;
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
