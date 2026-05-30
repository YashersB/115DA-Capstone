#include "gui.h"
#include "oxygen.h"
#include "LEDDriver.h"
#include <WiFi.h>

TaskHandle_t TaskSampling;
TaskHandle_t TaskDisplay;

volatile float temp = 0.0f;
volatile float currentSpO2 = 98.0f;
volatile int currentBPM = 72;
volatile float battery_voltage = 0.0f;

// Instantiate the LED Driver
LEDDriver ledDriver;

// Hardware Mapping for Multiplexer Control
#define MUX_A0 8   // GPIO 8 (A9)
#define MUX_A1 44  // GPIO 44 (D7)
#define ADC_PIN 1  // GPIO 1 (A0)
#define ADC_BAT 4  // GPIO 4 (A4)

int activeMuxChannel = 3;

// Resistor Divider Scaling Factors
const float DIVIDER_FACTOR_ADC = 2.0039801f;
const float DIVIDER_FACTOR_BAT = 2.0109f;

// Software Calibration offsets
const float PTAT_CAL_OFFSET = 0.042f;  
const float BATT_CAL_OFFSET = 0.0667f;  

// PTAT Constants
const float PTAT_BASELINE = 1.3188f;  
const float PTAT_SLOPE  = 0.0043f;  

void muxControl (int channel) {
    switch (channel) {
        case 1: digitalWrite(MUX_A0, LOW);  digitalWrite(MUX_A1, LOW);  break;
        case 2: digitalWrite(MUX_A0, HIGH); digitalWrite(MUX_A1, LOW);  break;
        case 3: digitalWrite(MUX_A0, LOW);  digitalWrite(MUX_A1, HIGH); break;
        case 4: digitalWrite(MUX_A0, HIGH); digitalWrite(MUX_A1, HIGH); break;
        default: return;
    }
    delayMicroseconds(10);
}

void processTempAndBattery() {
    long sum = 0;
    long sum_bat = 0; 
    const int numSamples = 64;

    for(int i = 0; i < numSamples; i++) {
        sum += analogReadMilliVolts(ADC_PIN);
        sum_bat += analogReadMilliVolts(ADC_BAT);
        delayMicroseconds(50); 
    }

    float rawAdc = (float)sum / numSamples;
    float rawBatAdc = (float)sum_bat / numSamples;

    float current_ptat = ((rawAdc / 1000.0f)) * DIVIDER_FACTOR_ADC + PTAT_CAL_OFFSET;
    float current_bat = ((rawBatAdc / 1000.0f)) * DIVIDER_FACTOR_BAT + BATT_CAL_OFFSET;
    
    static float smooth_ptat = 0.0f;
    static float smooth_bat = 0.0f;
    static bool firstRun = true;
    const float ALPHA = 0.05f; 

    if (firstRun) {
        smooth_ptat = current_ptat;
        smooth_bat  = current_bat;
        firstRun = false;
    }

    smooth_ptat = (ALPHA * current_ptat) + ((1.0f - ALPHA) * smooth_ptat);
    smooth_bat  = (ALPHA * current_bat)  + ((1.0f - ALPHA) * smooth_bat);

    temp = (smooth_ptat - PTAT_BASELINE) / PTAT_SLOPE;
    battery_voltage = smooth_bat;

    Serial.print("PTAT V: ");
    Serial.print(smooth_ptat, 3);
    Serial.print(" || Batt V: ");
    Serial.print(battery_voltage, 3);
    Serial.print(" || Temp C: ");
    Serial.println(temp, 1);
}

void setup() {
    Serial.begin(115200);
    analogSetAttenuation(ADC_11db);
    analogReadResolution(12);

    pinMode(MUX_A0, OUTPUT);
    pinMode(MUX_A1, OUTPUT);
    digitalWrite(MUX_A0, LOW);
    digitalWrite(MUX_A1, LOW);
    pinMode(ADC_BAT, INPUT);

    Wire.begin(5, 6);
    Wire.setClock(400000);
    setupGUI();

    oxygenInit();
    
    // Initialize the LED Driver pins
    //ledDriver.begin();

    WiFi.mode(WIFI_OFF);
    btStop();
}

void loop() {
    // 1. RUN THE LED DRIVER STATE MACHINE CONSTANTLY
    // This needs to execute as fast as possible to keep the microsecond timing accurate
   //ledDriver.update();

    // 2. NON-BLOCKING TIMER FOR TEMPERATURE AND PRINTING
    // Instead of delay(500), we check if 500ms have passed. 
    static unsigned long lastTempRead = 0;
    
    if (millis() - lastTempRead >= 500) {
        lastTempRead = millis(); // Reset the timer
        
        muxControl(3);
        processTempAndBattery();
    }
}