#include <Wire.h>

void setup() {
  delay(2000);
  Serial.begin(115200);
  Serial.println("\nI2C Scanner Starting...");
  Wire.begin(); // On XIAO C3, this uses D4 (SDA) and D5 (SCL)
}

void loop() {
  byte error, address;
  int nDevices = 0;

  Serial.println("Scanning...");

  for (address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    error = Wire.endTransmission();

    if (error == 0) {
      Serial.print("I2C device found at address 0x");
      if (address < 16) Serial.print("0");
      Serial.print(address, HEX);
      Serial.println("  !");
      nDevices++;
    }
  }

  if (nDevices == 0) Serial.println("No I2C devices found\n");
  else Serial.println("Done\n");

  delay(2000);           
}