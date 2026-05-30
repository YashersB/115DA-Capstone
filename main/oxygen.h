#ifndef OXYGEN_H
#define OXYGEN_H

#include <Arduino.h>

typedef struct {
  float spo2;
  float ratio;
  float dcRed;
  float dcIR;
  float acRed;
  float acIR;
  bool valid;
} spo2calc;

/*
 * For max accuracy, dont use linear lookup table for R, use 2nd order polynomial 
 * spo2 = aR^2 + bR + c
 * R = (acRed/dcRed)/(acIR/dcIR)
 * 
 * a, b, and c are calibration coeff
 * 
 */

void oxygenInit();

void oxygenSetCalibration(float a, float b, float c);

void oxygenAddSample(float redSample, float irSample);

bool oxygenReady();

spo2calc oxygencompute();

#endif
