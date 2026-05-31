// --- LEDDriver.h ---
#pragma once

#include <Arduino.h>

class LEDDriver {
  private:
    // LED Control Pins
    const uint8_t pinRed = 2; // GPIO 2 controls the Red LED
    const uint8_t pinIR = 3;  // GPIO 3 controls the IR LED

    unsigned long lastMicros;   // Tracks the last timestamp a state changed
    unsigned long currentDelay; // The duration to wait before the next state
    uint8_t phaseStep;          // Tracks the current phase of the LED cycle

  public:
    unsigned long settleTimeUS = 2000; // 2ms settle time to let LED fully turn on/off
    unsigned long readTimeUS = 500;    // 500us read window for the ADC

    LEDDriver() {
      lastMicros = 0;
      currentDelay = 0;
      phaseStep = 0;
    }

    uint8_t getPhase() const {
      // Returns the CURRENT active phase (0 = Red Settle, 1 = Red Read, etc.)
      return phaseStep;
    }

    void begin() {
      // Make sure the pins are not attached to hardware PWM
      pinMatrixOutDetach(pinRed, false, false);
      pinMatrixOutDetach(pinIR, false, false);

      // Set pins as digital outputs
      pinMode(pinRed, OUTPUT);
      pinMode(pinIR, OUTPUT);

      resetCycle(); // Start the state machine
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
      // Turn off IR, turn on Red
      digitalWrite(pinIR, LOW);
      digitalWrite(pinRed, HIGH);
    }

    void turnIROn() {
      // Turn off Red, turn on IR
      digitalWrite(pinRed, LOW);
      digitalWrite(pinIR, HIGH);
    }

    void turnAllOff() {
      // Turn both LEDs off to read ambient light
      digitalWrite(pinRed, LOW);
      digitalWrite(pinIR, LOW);
    }
};
