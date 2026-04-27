// Pulse Oximeter Capstone Design

#include "temperature.h"
#include "oxygen.h"
#include "cap_adc.h"
#include "ots_adc.h"
#include "gui.h"
#include "driver.h"

void setup() {
  Serial.begin(115200);
}

void loop() {
  Serial.println("XIAO S3 is alive!");
  delay(1000);
}