#pragma once

#include <Arduino.h>
#include <SPI.h>

// Future discrete-SAR interface.
// Define these in the board-specific source file once the external ADC path is
// wired into firmware. Keeping them declared here lets the SAR helper headers
// stay self-documenting without affecting the current ESP32-ADC-only build.

extern const uint8_t PIN_DAC_CS;
extern const uint8_t PIN_CDAC_LATCH;
extern const uint8_t PIN_COMP_OUT;

void setHoldMode();
void setTrackMode();
