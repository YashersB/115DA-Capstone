#ifndef OXYGEN_H
#define OXYGEN_H

#include <Arduino.h>
#include <math.h>

// Struct to hold calculated values
typedef struct {
  float spo2;
  float ratio;
  float dcRed;
  float dcIR;
  float acRed;
  float acIR;
  bool valid;
} spo2calc;

const int ADC_PIN_OXYGEN = A0;

void oxygenInit(); 
void oxygenAddSample(float redSample, float irSample); 
bool oxygenReady();
spo2calc oxygencompute();

#endif