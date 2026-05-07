#ifndef GUI_H
#define GUI_H

#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <Wire.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define i2c_Address 0x3c 

static Adafruit_SH1106G display = Adafruit_SH1106G(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
static volatile uint16_t ppgBuffer[SCREEN_WIDTH]; 

void setupGUI() {
    if(!display.begin(i2c_Address, true)) {
        Serial.println(F("SH1106 Failed"));
    }
    display.cp437(true); 
    display.clearDisplay();
}

void updateBuffer(uint16_t newSample) {
    // High-speed memory shift
    memmove((void*)ppgBuffer, (void*)(ppgBuffer + 1), (SCREEN_WIDTH - 1) * sizeof(uint16_t));
    ppgBuffer[SCREEN_WIDTH - 1] = newSample;
}

void drawGUI(float spo2, int bpm, float temp, int battMv) {
    display.clearDisplay();
    display.setTextColor(SH110X_WHITE);

    // 1. Battery (Original location)
    display.drawRect(105, 2, 18, 9, SH110X_WHITE);
    display.drawRect(123, 4, 2, 5, SH110X_WHITE);
    int barWidth = map(battMv, 3200, 4200, 0, 16);
    display.fillRect(106, 3, constrain(barWidth, 0, 16), 7, SH110X_WHITE);

    // 2. Vitals (Restored original locations)
    display.setTextSize(2);
    display.setCursor(0, 5);  display.print((int)spo2);
    display.setCursor(0, 25); display.print(bpm);
    
    display.setTextSize(1);
    display.setCursor(30, 5); display.print("SpO2%");
    display.setCursor(30, 25); display.print("BPM");

    // 3. Temperature (Restored original locations)
    display.setCursor(65, 25);
    display.setTextSize(2);
    display.print(temp, 1); 
    int x = display.getCursorX();
    display.setTextSize(1);
    display.setCursor(x + 1, 20); display.write(248); 
    display.setCursor(x + 8, 20); display.print("C");

    // 4. Waveform (Auto-Zoom logic kept, location fixed)
    uint16_t lMin = 4095, lMax = 0;
    for(int i = 0; i < SCREEN_WIDTH; i++) {
        if(ppgBuffer[i] < lMin) lMin = ppgBuffer[i];
        if(ppgBuffer[i] > lMax) lMax = ppgBuffer[i];
    }
    
    int range = lMax - lMin;
    if (range > 50) {
        for (int i = 0; i < SCREEN_WIDTH - 1; i++) {
            int y1 = map(ppgBuffer[i], lMin, lMax, 62, 46);
            int y2 = map(ppgBuffer[i+1], lMin, lMax, 62, 46);
            display.drawLine(i, y1, i+1, y2, SH110X_WHITE);
        }
    } else {
        display.drawLine(0, 54, 127, 54, SH110X_WHITE);
    }

    display.display();
}
#endif