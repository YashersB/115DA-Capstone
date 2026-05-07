#include "gui.h"

TaskHandle_t TaskSampling;
TaskHandle_t TaskDisplay;

volatile float currentTemp = 36.8;
volatile float currentSpO2 = 98.0;
volatile int currentBPM = 72;
volatile int currentBatt = 3800;

// ADJUST THIS FOR WAVEFORM SPEED
// 1 = Very fast, 4-6 = Condensed/Medical look, 10 = Slow
const int DECIMATION = 7; 

void samplingCode(void * pvParameters) {
    int sampleCounter = 0;
    for(;;) {
        // Your future analogRead(A0) goes here
        float t = millis() / 120.0; // Slightly slowed the sim frequency too
        uint16_t raw = 2048 + (sin(t) * 500) + (sin(t * 2.3) * 100); 
        
        // Decimation: Only push every N-th sample to the screen
        sampleCounter++;
        if(sampleCounter >= DECIMATION) {
            updateBuffer(raw);
            sampleCounter = 0;
        }
        
        vTaskDelay(pdMS_TO_TICKS(2)); // Still sampling at 500Hz for accuracy
    }
}

void displayCode(void * pvParameters) {
    for(;;) {
        drawGUI(currentSpO2, currentBPM, currentTemp, currentBatt);
        vTaskDelay(pdMS_TO_TICKS(33)); // Fixed 30 FPS refresh
    }
}

void setup() {
    Serial.begin(115200);
    analogSetAttenuation(ADC_11db);
    analogReadResolution(12);

    Wire.begin(5, 6);
    Wire.setClock(400000);
    setupGUI();

    xTaskCreatePinnedToCore(samplingCode, "Sampling", 4096, NULL, 3, &TaskSampling, 0);
    xTaskCreatePinnedToCore(displayCode, "Display", 4096, NULL, 1, &TaskDisplay, 1);
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}