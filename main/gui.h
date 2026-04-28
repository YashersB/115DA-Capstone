#ifndef GUI_H
#define GUI_H

#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <Wire.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define i2c_Address 0x3c 

// Initialize the SH1106 object
static Adafruit_SH1106G display = Adafruit_SH1106G(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Waveform Buffer for the PPG signal
static int ppgBuffer[128]; 

void setupGUI() {
    if(!display.begin(i2c_Address, true)) { 
        Serial.println(F("SH1106 initialization failed"));
    }
    display.cp437(true); // Enable extended ASCII for the degree symbol
    display.clearDisplay();
    display.display();
}

void updateBuffer(int newSample) {
    for (int i = 0; i < 127; i++) {
        ppgBuffer[i] = ppgBuffer[i+1];
    }
    ppgBuffer[127] = newSample;
}

void drawGUI(float spo2, int bpm, float temp, int battMv) {
    display.clearDisplay();
    display.setTextColor(SH110X_WHITE);

    // BATTERY ICON (Top Right)
    // Range: 3200mV (Empty) to 4200mV (Full)
    display.drawRect(105, 2, 18, 9, SH110X_WHITE); // Battery Body
    display.drawRect(123, 4, 2, 5, SH110X_WHITE);  // Battery Tip
    
    int barWidth = map(battMv, 3200, 4200, 0, 16);
    barWidth = constrain(barWidth, 0, 16);
    display.fillRect(106, 3, barWidth, 7, SH110X_WHITE);

    // VITALS (Left Side)
    // SpO2
    display.setCursor(0, 5);
    display.setTextSize(2); display.print((int)spo2); 
    display.setTextSize(1); display.setCursor(display.getCursorX()+2, 12); 
    display.print("SpO2%");

    // Heart Rate
    display.setCursor(0, 25);
    display.setTextSize(2); display.print(bpm); 
    display.setTextSize(1); display.setCursor(display.getCursorX()+2, 32); 
    display.print("BPM");

    // TEMPERATURE (Middle-Right)
    display.setCursor(55, 25);
    display.setTextSize(2);
    display.print(temp, 1); 
    
    // Custom Degree Symbol Logic
    int x = display.getCursorX();
    int y = display.getCursorY();
    display.setTextSize(1);
    display.setCursor(x + 1, y);
    display.write(248); // Clean degree circle
    display.setTextSize(2);
    display.setCursor(x + 7, y);
    display.print("F");

    // WAVEFORM PLOT (Bottom)
    for (int i = 0; i < 127; i++) {
        // Map 0-1024 raw ADC data to the bottom 18 pixels (y=45 to y=63)
        int y1 = map(ppgBuffer[i], 0, 1024, 63, 45);
        int y2 = map(ppgBuffer[i+1], 0, 1024, 63, 45);
        display.drawLine(i, y1, i+1, y2, SH110X_WHITE);
    }

    display.display();
}

#endif