#include <Arduino.h>
#include <WiFi.h>

#include "gui.h"
#include "LEDDriver.h"
#include "oxygen.h"

namespace {

constexpr uint8_t MUX_A0 = 8;
constexpr uint8_t MUX_A1 = 44;
constexpr uint8_t ADC_PIN = 1;
constexpr uint8_t ADC_BAT = 4;

constexpr uint8_t MUX_CHANNEL_AC = 1;
constexpr uint8_t MUX_CHANNEL_DC = 2;
constexpr uint8_t MUX_CHANNEL_PTAT = 3;
constexpr uint8_t MUX_CHANNEL_AUX = 4;

constexpr uint16_t MUX_SETTLE_US = 10;
constexpr uint16_t ADC_RECOVERY_US = 50;
constexpr uint16_t PTAT_SAMPLE_COUNT = 64;
constexpr uint32_t TEMP_READ_INTERVAL_MS = 500;
constexpr uint8_t ALGORITHM_DOWNSAMPLE = 5; // Accumulate and average this many frames (e.g. 5 = 20Hz effective rate)
constexpr uint8_t WAVEFORM_DECIMATION = 2; // Decimate for OLED GUI display
constexpr bool ENABLE_PPG_DEBUG = true; // Set to true to see SpO2, BPM, Temp, Battery in the Serial Monitor
constexpr bool ENABLE_SERIAL_PLOTTER = false; // Set to true to plot AC waveforms in Arduino Serial Plotter
constexpr uint32_t DEBUG_PRINT_INTERVAL_MS = 250;

constexpr float DIVIDER_FACTOR_ADC = 2.0039801f;
constexpr float DIVIDER_FACTOR_BAT = 2.0109f;
constexpr float PTAT_CAL_OFFSET = 0.042f;
constexpr float BATT_CAL_OFFSET = 0.0667f;
constexpr float PTAT_BASELINE = 1.3188f;
constexpr float PTAT_SLOPE = 0.0043f;

struct PpgFrameAccumulator {
    uint32_t redAcSum = 0, irAcSum = 0, amb1AcSum = 0, amb2AcSum = 0;
    uint32_t redDcSum = 0, irDcSum = 0, amb1DcSum = 0, amb2DcSum = 0;
    
    uint16_t redAcCount = 0, irAcCount = 0, amb1AcCount = 0, amb2AcCount = 0;
    uint16_t redDcCount = 0, irDcCount = 0, amb1DcCount = 0, amb2DcCount = 0;
    
    uint8_t decimationCounter = 0;
    bool cycleComplete = false;
};

// Professional Digital Bandpass Filter for Peak Detection
class BpmCalculator {
private:
    // Filter states
    float x_prev = 0.0f;
    float y_hp_prev = 0.0f;
    float y_lp_prev = 0.0f;

    float lastFiltered = 0.0f;
    float prevFiltered = 0.0f;

    uint32_t lastPeakTimeMs = 0;
    float peakThreshold = 10.0f; 

    static const uint8_t BPM_AVG_SIZE = 16;
    uint8_t bpmBuffer[BPM_AVG_SIZE];
    uint8_t bpmIdx = 0;
    uint8_t validBpmCount = 0;

public:
    BpmCalculator() {
        for(int i=0; i<BPM_AVG_SIZE; i++) bpmBuffer[i] = 0;
    }

    uint8_t getBPM() {
        if (validBpmCount == 0) return 0; // Return 0 until we get real beats
        uint16_t sum = 0;
        for(int i=0; i<validBpmCount; i++) sum += bpmBuffer[i];
        return sum / validBpmCount;
    }

    void addSample(float acValue) {
        // 1. High-Pass Filter (DC Blocker) at 0.5 Hz (Removes baseline wander/breathing artifacts)
        // Calculated for Fs = 20Hz, cutoff = 0.5Hz. alpha_hp = 0.864
        float y_hp = 0.864f * y_hp_prev + 0.864f * (acValue - x_prev);
        x_prev = acValue;
        y_hp_prev = y_hp;

        // 2. Low-Pass Filter at 3.5 Hz (Removes high-frequency electrical/ADC noise)
        // Calculated for Fs = 20Hz, cutoff = 3.5Hz. alpha_lp = 0.524
        float filtered = 0.524f * y_hp + (1.0f - 0.524f) * y_lp_prev;
        y_lp_prev = filtered;

        // 3. Peak detection (looking for a local maximum on the beautifully filtered wave)
        if (prevFiltered > lastFiltered && prevFiltered > filtered && prevFiltered > peakThreshold) {
            uint32_t now = millis();
            uint32_t delta = now - lastPeakTimeMs;

            // Refractory period: at least 400ms (max 150 BPM) to prevent double-counting dicrotic notches
            if (delta > 400) {
                float instantBpm = 60000.0f / delta;

                // Validate realistic BPM (e.g., 40 to 150)
                if (instantBpm >= 40 && instantBpm <= 150) {
                    bpmBuffer[bpmIdx] = (uint8_t)instantBpm;
                    bpmIdx = (bpmIdx + 1) % BPM_AVG_SIZE;
                    if (validBpmCount < BPM_AVG_SIZE) validBpmCount++;
                }
                
                lastPeakTimeMs = now;
                // Dynamic threshold: 75% of the peak, decays over time so it adapts to different fingers
                peakThreshold = prevFiltered * 0.75f;
            }
        } else {
            // Decay the threshold slowly
            peakThreshold *= 0.99f;
            if (peakThreshold < 2.0f) peakThreshold = 2.0f; // Lowered floor because bandpass limits amplitude
        }

        lastFiltered = prevFiltered;
        prevFiltered = filtered;
    }
};

BpmCalculator bpmCalc;


TaskHandle_t taskDisplay = nullptr;
LEDDriver ledDriver;
PpgFrameAccumulator ppgFrame;

// Global sensor values
float currentSpO2 = 0.0f;
int currentBPM = 0; // Starts at 0, updates with real heartbeats
float tempC = 0.0f;
int batteryMilliVolts = 3800;

uint8_t currentMuxChannel = 0;
bool ptatReadPending = false;
unsigned long lastTempReadMs = 0;
unsigned long lastDebugPrintMs = 0;

void resetPpgFrame() {
    ppgFrame.redAcSum = 0; ppgFrame.irAcSum = 0; ppgFrame.amb1AcSum = 0; ppgFrame.amb2AcSum = 0;
    ppgFrame.redDcSum = 0; ppgFrame.irDcSum = 0; ppgFrame.amb1DcSum = 0; ppgFrame.amb2DcSum = 0;
    
    ppgFrame.redAcCount = 0; ppgFrame.irAcCount = 0; ppgFrame.amb1AcCount = 0; ppgFrame.amb2AcCount = 0;
    ppgFrame.redDcCount = 0; ppgFrame.irDcCount = 0; ppgFrame.amb1DcCount = 0; ppgFrame.amb2DcCount = 0;
    
    ppgFrame.cycleComplete = false;
}

void selectMuxChannel(uint8_t channel) {
    if (channel == currentMuxChannel) {
        return;
    }

    switch (channel) {
        case MUX_CHANNEL_AC:
            digitalWrite(MUX_A0, LOW);
            digitalWrite(MUX_A1, LOW);
            break;

        case MUX_CHANNEL_DC:
            digitalWrite(MUX_A0, HIGH);
            digitalWrite(MUX_A1, LOW);
            break;

        case MUX_CHANNEL_PTAT:
            digitalWrite(MUX_A0, LOW);
            digitalWrite(MUX_A1, HIGH);
            break;

        case MUX_CHANNEL_AUX:
            digitalWrite(MUX_A0, HIGH);
            digitalWrite(MUX_A1, HIGH);
            break;

        default:
            return;
    }

    currentMuxChannel = channel;
    delayMicroseconds(MUX_SETTLE_US);
}

float readMuxAverageMilliVolts(uint8_t channel, uint16_t sampleCount) {
    uint32_t sum = 0;

    selectMuxChannel(channel);

    for (uint16_t i = 0; i < sampleCount; i++) {
        sum += analogReadMilliVolts(ADC_PIN);
        delayMicroseconds(ADC_RECOVERY_US);
    }

    return static_cast<float>(sum) / sampleCount;
}

float readBatteryAverageMilliVolts(uint16_t sampleCount) {
    uint32_t sum = 0;

    for (uint16_t i = 0; i < sampleCount; i++) {
        sum += analogReadMilliVolts(ADC_BAT);
        delayMicroseconds(ADC_RECOVERY_US);
    }

    return static_cast<float>(sum) / sampleCount;
}

void updateTempAndBattery(float rawPtatMilliVolts, float rawBatteryMilliVolts) {
    float currentPtat = (rawPtatMilliVolts / 1000.0f) * DIVIDER_FACTOR_ADC + PTAT_CAL_OFFSET;
    float currentBattery = (rawBatteryMilliVolts / 1000.0f) * DIVIDER_FACTOR_BAT + BATT_CAL_OFFSET;

    static float smoothPtat = 0.0f;
    static float smoothBattery = 0.0f;
    static bool firstRun = true;
    constexpr float ALPHA = 0.05f;

    if (firstRun) {
        smoothPtat = currentPtat;
        smoothBattery = currentBattery;
        firstRun = false;
    }

    smoothPtat = (ALPHA * currentPtat) + ((1.0f - ALPHA) * smoothPtat);
    smoothBattery = (ALPHA * currentBattery) + ((1.0f - ALPHA) * smoothBattery);

    tempC = (smoothPtat - PTAT_BASELINE) / PTAT_SLOPE;
    batteryMilliVolts = static_cast<int>(smoothBattery * 1000.0f);
}

void serviceSlowAnalogChannels() {
    float rawPtatMilliVolts = readMuxAverageMilliVolts(MUX_CHANNEL_PTAT, PTAT_SAMPLE_COUNT);
    float rawBatteryMilliVolts = readBatteryAverageMilliVolts(PTAT_SAMPLE_COUNT);

    updateTempAndBattery(rawPtatMilliVolts, rawBatteryMilliVolts);

    // We don't restore MUX to PPG here because the fast loop toggles AC/DC dynamically
    resetPpgFrame();
    ledDriver.resetCycle();
}

void maybePrintDebug(const spo2calc &result, float trueRed, float trueIR) {
    if (!ENABLE_PPG_DEBUG) {
        return;
    }

    unsigned long now = millis();
    if (now - lastDebugPrintMs < DEBUG_PRINT_INTERVAL_MS) {
        return;
    }

    Serial.printf(
        "SpO2: %.1f %% | BPM: %d | Ratio: %.3f | rAC: %.1f | rDC: %.1f | iAC: %.1f | iDC: %.1f\n",
        result.spo2,
        currentBPM,
        result.ratio,
        trueRed, // AC
        result.dcRed, // DC baseline from the algorithm
        trueIR,  // AC
        result.dcIR); // DC baseline from the algorithm

    lastDebugPrintMs = now;
}

void processCompletedPpgFrame() {
    if (ppgFrame.redAcCount == 0 || ppgFrame.amb1AcCount == 0 || ppgFrame.redDcCount == 0 || ppgFrame.amb1DcCount == 0) {
        resetPpgFrame();
        return;
    }

    // Average the AC windows
    float redAcAvg = static_cast<float>(ppgFrame.redAcSum) / ppgFrame.redAcCount;
    float amb1AcAvg = static_cast<float>(ppgFrame.amb1AcSum) / ppgFrame.amb1AcCount;
    float irAcAvg = static_cast<float>(ppgFrame.irAcSum) / ppgFrame.irAcCount;
    float amb2AcAvg = static_cast<float>(ppgFrame.amb2AcSum) / ppgFrame.amb2AcCount;

    // Average the DC windows
    float redDcAvg = static_cast<float>(ppgFrame.redDcSum) / ppgFrame.redDcCount;
    float amb1DcAvg = static_cast<float>(ppgFrame.amb1DcSum) / ppgFrame.amb1DcCount;
    float irDcAvg = static_cast<float>(ppgFrame.irDcSum) / ppgFrame.irDcCount;
    float amb2DcAvg = static_cast<float>(ppgFrame.amb2DcSum) / ppgFrame.amb2DcCount;

    // Subtract ambient AC (rejects 50/60Hz optical noise)
    float trueRedAc = redAcAvg - amb1AcAvg;
    float trueIrAc = irAcAvg - amb2AcAvg;

    // Subtract ambient DC (rejects baseline room lighting)
    float trueRedDc = redDcAvg - amb1DcAvg;
    float trueIrDc = irDcAvg - amb2DcAvg;

    // Downsample (average) multiple frames to slow down the data rate, filter noise, and give the buffer more time history
    static uint8_t downsampleCount = 0;
    static float dsRedAcSum = 0, dsRedDcSum = 0, dsIrAcSum = 0, dsIrDcSum = 0;

    dsRedAcSum += trueRedAc;
    dsRedDcSum += trueRedDc;
    dsIrAcSum += trueIrAc;
    dsIrDcSum += trueIrDc;

    downsampleCount++;
    if (downsampleCount >= ALGORITHM_DOWNSAMPLE) {
        float finalRedAc = dsRedAcSum / ALGORITHM_DOWNSAMPLE;
        float finalRedDc = dsRedDcSum / ALGORITHM_DOWNSAMPLE;
        float finalIrAc = dsIrAcSum / ALGORITHM_DOWNSAMPLE;
        float finalIrDc = dsIrDcSum / ALGORITHM_DOWNSAMPLE;

        // Feed the Red AC signal into the BPM calculator
        bpmCalc.addSample(finalRedAc);
        currentBPM = bpmCalc.getBPM();

        oxygenAddSample(finalRedAc, finalRedDc, finalIrAc, finalIrDc);

        // Print for Arduino Serial Plotter
        if (ENABLE_SERIAL_PLOTTER) {
            Serial.printf("RedAC:%.2f,IrAC:%.2f\n", finalRedAc, finalIrAc);
        }

        if (oxygenReady()) {
            spo2calc result = oxygencompute();
            if (result.valid) {
                currentSpO2 = result.spo2;
            }
            // Always print debug stats so we can see if DC is falling to 0
            maybePrintDebug(result, finalRedAc, finalIrAc);
        }

        // Reset downsample accumulators
        dsRedAcSum = 0; dsRedDcSum = 0; dsIrAcSum = 0; dsIrDcSum = 0;
        downsampleCount = 0;
    }

    ppgFrame.decimationCounter++;
    if (ppgFrame.decimationCounter >= WAVEFORM_DECIMATION) {
        // Offset the AC waveform so it plots nicely in the GUI (since it swings around 0)
        uint16_t waveformSample = static_cast<uint16_t>(constrain(trueIrAc + 2048.0f, 0.0f, 4095.0f));
        updateBuffer(waveformSample);
        ppgFrame.decimationCounter = 0;
    }

    resetPpgFrame();
}

void processPpgChannel() {
    ledDriver.update();

    // Toggle flag to alternate reading AC and DC as fast as the loop runs
    static bool sampleAC = true;

    switch (ledDriver.getPhase()) {
        case 1: // Red Read Window (500us)
            if (sampleAC) {
                selectMuxChannel(MUX_CHANNEL_AC);
                ppgFrame.redAcSum += analogReadMilliVolts(ADC_PIN);
                ppgFrame.redAcCount++;
            } else {
                selectMuxChannel(MUX_CHANNEL_DC);
                ppgFrame.redDcSum += analogReadMilliVolts(ADC_PIN);
                ppgFrame.redDcCount++;
            }
            sampleAC = !sampleAC;
            break;

        case 3: // Ambient 1 Read Window (500us)
            if (sampleAC) {
                selectMuxChannel(MUX_CHANNEL_AC);
                ppgFrame.amb1AcSum += analogReadMilliVolts(ADC_PIN);
                ppgFrame.amb1AcCount++;
            } else {
                selectMuxChannel(MUX_CHANNEL_DC);
                ppgFrame.amb1DcSum += analogReadMilliVolts(ADC_PIN);
                ppgFrame.amb1DcCount++;
            }
            sampleAC = !sampleAC;
            break;

        case 5: // IR Read Window (500us)
            if (sampleAC) {
                selectMuxChannel(MUX_CHANNEL_AC);
                ppgFrame.irAcSum += analogReadMilliVolts(ADC_PIN);
                ppgFrame.irAcCount++;
            } else {
                selectMuxChannel(MUX_CHANNEL_DC);
                ppgFrame.irDcSum += analogReadMilliVolts(ADC_PIN);
                ppgFrame.irDcCount++;
            }
            sampleAC = !sampleAC;
            break;

        case 7: // Ambient 2 Read Window (500us)
            if (sampleAC) {
                selectMuxChannel(MUX_CHANNEL_AC);
                ppgFrame.amb2AcSum += analogReadMilliVolts(ADC_PIN);
                ppgFrame.amb2AcCount++;
            } else {
                selectMuxChannel(MUX_CHANNEL_DC);
                ppgFrame.amb2DcSum += analogReadMilliVolts(ADC_PIN);
                ppgFrame.amb2DcCount++;
            }
            sampleAC = !sampleAC;
            ppgFrame.cycleComplete = true;
            break;

        case 0: // Red Settle Window (2000us) - Start of a new cycle
            if (ppgFrame.cycleComplete) {
                processCompletedPpgFrame();
            }
            break;

        default:
            break;
    }
}

void maybeServiceTelemetry() {
    unsigned long now = millis();

    if (now - lastTempReadMs >= TEMP_READ_INTERVAL_MS) {
        ptatReadPending = true;
    }

    if (!ptatReadPending) {
        return;
    }

    if (ledDriver.getPhase() != 0) { // Wait until we enter Phase 0 (Red Settle)
        return;
    }

    serviceSlowAnalogChannels();
    lastTempReadMs = millis();
    ptatReadPending = false;
}

void displayCode(void *pvParameters) {
    (void)pvParameters;

    for (;;) {
        drawGUI(currentSpO2, currentBPM, tempC, batteryMilliVolts);
        vTaskDelay(pdMS_TO_TICKS(33));
    }
}

} // namespace

void setup() {
    Serial.begin(115200);

    analogSetAttenuation(ADC_11db);
    analogReadResolution(12);

    pinMode(MUX_A0, OUTPUT);
    pinMode(MUX_A1, OUTPUT);
    pinMode(ADC_PIN, INPUT);
    pinMode(ADC_BAT, INPUT);

    digitalWrite(MUX_A0, LOW);
    digitalWrite(MUX_A1, LOW);
    selectMuxChannel(MUX_CHANNEL_AC);

    oxygenInit();
    oxygenSetCalibration(0.0f, -25.0f, 110.0f);

    Wire.begin(5, 6);
    Wire.setClock(400000);
    setupGUI();

    WiFi.mode(WIFI_OFF);
    btStop();

    ledDriver.begin();

    xTaskCreatePinnedToCore(displayCode, "Display", 8192, nullptr, 1, &taskDisplay, 0);
}

void loop() {
    processPpgChannel();
    maybeServiceTelemetry();
}
