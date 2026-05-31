#pragma once
#include <Arduino.h> 
#include "driver/gpio.h"

class LEDDriver {
  private:
    const gpio_num_t pinRed = GPIO_NUM_2; 
    const gpio_num_t pinIR  = GPIO_NUM_3;  

    unsigned long lastMicros;
    unsigned long currentDelay;
    uint8_t phaseStep;

  public:
    unsigned long settleTimeUS = 5000; 
    unsigned long readTimeUS = 20000;    

    LEDDriver() {
      lastMicros = 0;
      currentDelay = 0;
      phaseStep = 0;
    }

    void begin() {
      gpio_reset_pin(pinRed);
      gpio_reset_pin(pinIR);

      gpio_set_direction(pinRed, GPIO_MODE_OUTPUT);
      gpio_set_direction(pinIR, GPIO_MODE_OUTPUT);

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
      gpio_set_level(pinIR, 0);     
      gpio_set_level(pinRed, 1);   
    }

    void turnIROn() {
      gpio_set_level(pinRed, 0);    
      gpio_set_level(pinIR, 1);    
    }

    void turnAllOff() {
      gpio_set_level(pinRed, 0);
      gpio_set_level(pinIR, 0);
    }
};