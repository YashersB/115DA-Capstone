#include "gui.h"

// --- Placeholder Vitals Data ---
float readPTAT() { return 95.6; }          // Dummy Temperature
float calculateOxygen() { return 98.5; }   // Dummy SpO2
int calculateHeartRate() { return 72; }    // Dummy BPM
int readBatteryMv() { return 4200; }       // Dummy Battery Voltage mV (3.3 to 4.2 range)

void setup() {
    delay(2000); 
    Serial.begin(115200);

    // Set ADC range from 0 to 3.3V
    analogSetAttenuation(ADC_11db); 
    
    // XIAO ESP32-C3 I2C: SDA=GPIO6 (D4), SCL=GPIO7 (D5)
    Wire.begin(6, 7); 
    Wire.setClock(400000); // 400kHz Fast Mode
    
    setupGUI();
    Serial.println("Capstone Pulse Ox: System Online");
}

void loop() {
    //Gather all data
    float currentTemp = readPTAT();
    float currentSpO2 = calculateOxygen();
    int currentBPM = calculateHeartRate();
    int batteryMv = readBatteryMv();

    //Simulate Waveform (Merging two Sine waves for a "Pulse" look)
    float time = millis() / 150.0;
    int rawPPG = 512 + (sin(time) * 200) + (sin(time * 2.5) * 50);
    
    updateBuffer(rawPPG);

    // Draw the GUI
    drawGUI(currentSpO2, currentBPM, currentTemp, batteryMv);

    // Short delay to keep the loop responsive
    delay(10); 
}