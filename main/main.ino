#include "gui.h"
#include <WiFi.h>

TaskHandle_t TaskSampling;
TaskHandle_t TaskDisplay;

volatile float temp = 0.0f;
volatile float currentSpO2 = 98.0;
volatile int currentBPM = 72;
volatile float battery_voltage = 0.0f;

// Hardware Mapping for Multiplexer Control
#define MUX_A0 8   // GPIO 8 (A9)
#define MUX_A1 44  // GPIO 44 (D7)
#define ADC_PIN 1 // GPIO 1 (A0)
#define ADC_BAT 4 // GPIO 4 (A4)

// Resistor Divider Scaling Factors
const float DIVIDER_FACTOR_ADC = 2.0039801f;
const float DIVIDER_FACTOR_BAT = 2.0109f;

// Software Calibration offsets
const float PTAT_CAL_OFFSET = 0.042f;  
const float BATT_CAL_OFFSET = 0.0667f;  

// PTAT Constants
const float PTAT_BASELINE = 1.3188f;  
const float PTAT_SLOPE  = 0.0043f;  

// ADJUST THIS FOR WAVEFORM SPEED
// 1 = Very fast, 4-6 = Condensed/Medical look, 10 = Slow
const int DECIMATION = 7; 

void muxControl (int channel) {
  switch (channel) {
        case 1: // Binary: 00
            digitalWrite(MUX_A0, LOW);
            digitalWrite(MUX_A1, LOW);
            break;
            
        case 2: // Binary: 01
            digitalWrite(MUX_A0, HIGH);
            digitalWrite(MUX_A1, LOW);
            break;
            
        case 3: // Binary: 10
            digitalWrite(MUX_A0, LOW);
            digitalWrite(MUX_A1, HIGH);
            break;
            
        case 4: // Binary: 11
            digitalWrite(MUX_A0, HIGH);
            digitalWrite(MUX_A1, HIGH);
            break;
            
        default:
            return;
    }
    delayMicroseconds(10);
}
/*
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
        drawGUI(currentSpO2, currentBPM, temp, battery_voltage);
        vTaskDelay(pdMS_TO_TICKS(33)); // Fixed 30 FPS refresh
    }
}
*/
void setup() {
    Serial.begin(115200);
    analogSetAttenuation(ADC_11db);
    analogReadResolution(12);

    //MUX Pins
    pinMode(MUX_A0, OUTPUT);
    pinMode(MUX_A1, OUTPUT);
    digitalWrite(MUX_A0, LOW);
    digitalWrite(MUX_A1, LOW);

    //Battery ADC Pin
    pinMode(ADC_BAT, INPUT);

    Wire.begin(5, 6);
    Wire.setClock(400000);
    setupGUI();

    //Turn off WIFI and Bluetooth
    WiFi.mode(WIFI_OFF);
    btStop();

    //xTaskCreatePinnedToCore(samplingCode, "Sampling", 4096, NULL, 3, &TaskSampling, 0);
    //xTaskCreatePinnedToCore(displayCode, "Display", 4096, NULL, 1, &TaskDisplay, 1);
}

void loop() {
    //vTaskDelay(pdMS_TO_TICKS(1000));
    muxControl(3);

    // ESP ADC Sampling
    long sum = 0;
    long sum_bat = 0; 
    const int numSamples = 64;

    for(int i = 0; i < numSamples; i++) {
        sum += analogReadMilliVolts(ADC_PIN);
        sum_bat += analogReadMilliVolts(ADC_BAT);
        delayMicroseconds(50); // Gives the internal ADC time to reset
    }

    // Calculate averages
    float rawAdc = (float)sum / numSamples;
    float rawBatAdc = (float)sum_bat / numSamples;

    // Convert to true voltages 
    float current_ptat = ((rawAdc / 1000.0f)) * DIVIDER_FACTOR_ADC + PTAT_CAL_OFFSET;
    float current_bat = ((rawBatAdc / 1000.0f)) * DIVIDER_FACTOR_BAT + BATT_CAL_OFFSET;
    
    // --- EXPONENTIAL MOVING AVERAGE (EMA) FILTER ---
    // These static variables remember their state between loop executions
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

    // PTAT Math and Battery Voltage
    temp = (smooth_ptat-PTAT_BASELINE) / PTAT_SLOPE;
    battery_voltage = smooth_bat;

    // Output to Serial
    Serial.print("PTAT True V: ");
    Serial.print(smooth_ptat, 3);
    Serial.print(" V  ||  Battery True V: ");
    Serial.print(battery_voltage, 3);
    Serial.println(" V");

    Serial.print(" || Celcius C: ");
    Serial.println(temp, 1);

    // Read twice per second
    delay(500);

}