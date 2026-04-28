#ifndef GUI_H
#define GUI_H

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// Only define the display once here
static Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Waveform Buffer and Index
static int ppgBuffer[128]; 

void setupGUI() {
    if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
        Serial.println(F("OLED failed"));
        for(;;); 
    }
    display.clearDisplay();
    display.display();
}

void updateBuffer(int newSample) {
    for (int i = 0; i < 127; i++) {
        ppgBuffer[i] = ppgBuffer[i+1];
    }
    ppgBuffer[127] = newSample;
}

void drawGUI(float spo2, int bpm, float temp, int batt) {
    display.clearDisplay();

    // Battery Icon
    display.drawRect(110, 2, 15, 7, SSD1306_WHITE);
    display.drawRect(125, 4, 2, 3, SSD1306_WHITE);
    int battWidth = map(batt, 0, 100, 0, 13);
    display.fillRect(111, 3, battWidth, 5, SSD1306_WHITE);

    // Temperature (Replacing Pi)
    display.setCursor(75, 25);
    display.setTextSize(2);
    display.print(temp, 1);
    display.setTextSize(1); display.print(" C");

    // PR and SpO2
    display.setCursor(0, 10);
    display.setTextSize(2); display.print((int)spo2);
    display.setTextSize(1); display.print(" SpO2%");

    display.setCursor(0, 30);
    display.setTextSize(2); display.print(bpm);
    display.setTextSize(1); display.print(" BPM");

    // Waveform Plot
    for (int i = 0; i < 127; i++) {
        int y1 = map(ppgBuffer[i], 0, 1024, 63, 48);
        int y2 = map(ppgBuffer[i+1], 0, 1024, 63, 48);
        display.drawLine(i, y1, i+1, y2, SSD1306_WHITE);
    }

    display.display();
}

#endif