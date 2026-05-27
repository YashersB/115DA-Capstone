// Blood Oxygen Level Algorithm : Adam

//Libraries
//////////////////////////////////////////////////////////

#ifndef OXYGEN_H
#define OXYGEN_H
#include <math.h>
#include "driver.h"
#include "gui.h"
#include "cap_adc.h"
#include "ots_adc.h"

*************************************
//LED Driver: controls LED timing and source
//main.cpp gather sample from red/IR
//this-> computes AC/DC/R/SpO2

typedef struct{
  float spo2;
  float ratio;
  float dcRed;
  float dcIR;
  float acRed;
  float acIR;
  bool valid;
}spo2calc;

/*
 * For max accuracy, dont use linear lookup table for R, use 2nd order polynomial 
 * spo2 = aR^2 + bR + c
 * R = (acRed/dcRed)/(acIR/dcIR)
 * 
 * a, b, and c are calibration coeff
 * 
 */

//use output of MUX and short to input of esp gpio - adc_bypass to esp
//Pin: GPIO_A0_D0
//
const int ADC_PIN = A0;


void oxygenInit();  //initialization function

void oxygenAddSample(float redSample, float irSample); //

bool oxygenReady();

spo2calc oxygencompute();

#endif