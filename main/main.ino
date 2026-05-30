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

constexpr uint8_t MUX_CHANNEL_PPG = 1;
constexpr uint8_t MUX_CHANNEL_BPM = 2;
constexpr uint8_t MUX_CHANNEL_PTAT = 3;
constexpr uint8_t MUX_CHANNEL_AUX = 4;

constexpr uint16_t MUX_SETTLE_US = 10;
constexpr uint16_t ADC_RECOVERY_US = 50;
constexpr uint16_t PTAT_SAMPLE_COUNT = 64;
constexpr uint32_t TEMP_READ_INTERVAL_MS = 500;
constexpr uint8_t WAVEFORM_DECIMATION = 6;
constexpr bool ENABLE_PPG_DEBUG = false;
constexpr uint32_t DEBUG_PRINT_INTERVAL_MS = 250;

constexpr float DIVIDER_FACTOR_ADC = 2.0039801f;
constexpr float DIVIDER_FACTOR_BAT = 2.0109f;
constexpr float PTAT_CAL_OFFSET = 0.042f;
constexpr float BATT_CAL_OFFSET = 0.0667f;
constexpr float PTAT_BASELINE = 1.3188f;
constexpr float PTAT_SLOPE = 0.0043f;

struct PpgFrameAccumulator {
    uint32_t redSum = 0;
    uint32_t irSum = 0;
    uint32_t amb1Sum = 0;
    uint32_t amb2Sum = 0;
    uint16_t redCount = 0;
    uint16_t irCount = 0;
    uint16_t amb1Count = 0;
    uint16_t amb2Count = 0;
    uint8_t decimationCounter = 0;
    bool cycleComplete = false;
};

TaskHandle_t taskDisplay = nullptr;
LEDDriver ledDriver;
PpgFrameAccumulator ppgFrame;

volatile float tempC = 0.0f;
volatile float currentSpO2 = 98.0f;
volatile int currentBPM = 72;
volatile int batteryMilliVolts = 3800;

uint8_t currentMuxChannel = 0;
bool ptatReadPending = false;
unsigned long lastTempReadMs = 0;
unsigned long lastDebugPrintMs = 0;

void resetPpgFrame() {
    ppgFrame.redSum = 0;
    ppgFrame.irSum = 0;
    ppgFrame.amb1Sum = 0;
    ppgFrame.amb2Sum = 0;
    ppgFrame.redCount = 0;
    ppgFrame.irCount = 0;
    ppgFrame.amb1Count = 0;
    ppgFrame.amb2Count = 0;
    ppgFrame.cycleComplete = false;
}

void selectMuxChannel(uint8_t channel) {
    if (channel == currentMuxChannel) {
        return;
    }

    switch (channel) {
        case MUX_CHANNEL_PPG:
            digitalWrite(MUX_A0, LOW);
            digitalWrite(MUX_A1, LOW);
            break;

        case MUX_CHANNEL_BPM:
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

    selectMuxChannel(MUX_CHANNEL_PPG);
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
        "SpO2: %.1f %% | Ratio: %.3f | trueRed: %.2f | trueIR: %.2f | counts R/A1/IR/A2 = %u/%u/%u/%u\n",
        result.spo2,
        result.ratio,
        trueRed,
        trueIR,
        ppgFrame.redCount,
        ppgFrame.amb1Count,
        ppgFrame.irCount,
        ppgFrame.amb2Count);

    lastDebugPrintMs = now;
}

void processCompletedPpgFrame() {
    if (ppgFrame.redCount == 0 || ppgFrame.amb1Count == 0 ||
        ppgFrame.irCount == 0 || ppgFrame.amb2Count == 0) {
        resetPpgFrame();
        return;
    }

    float redAvg = static_cast<float>(ppgFrame.redSum) / ppgFrame.redCount;
    float amb1Avg = static_cast<float>(ppgFrame.amb1Sum) / ppgFrame.amb1Count;
    float irAvg = static_cast<float>(ppgFrame.irSum) / ppgFrame.irCount;
    float amb2Avg = static_cast<float>(ppgFrame.amb2Sum) / ppgFrame.amb2Count;

    float trueRed = redAvg - amb1Avg;
    float trueIR = irAvg - amb2Avg;

    if (trueRed < 0.0f) trueRed = 0.0f;
    if (trueIR < 0.0f) trueIR = 0.0f;

    oxygenAddSample(trueRed, trueIR);

    if (oxygenReady()) {
        spo2calc result = oxygencompute();
        if (result.valid) {
            currentSpO2 = result.spo2;
            maybePrintDebug(result, trueRed, trueIR);
        }
    }

    ppgFrame.decimationCounter++;
    if (ppgFrame.decimationCounter >= WAVEFORM_DECIMATION) {
        uint16_t waveformSample = static_cast<uint16_t>(constrain(trueIR, 0.0f, 4095.0f));
        updateBuffer(waveformSample);
        ppgFrame.decimationCounter = 0;
    }

    resetPpgFrame();
}

void processPpgChannel() {
    ledDriver.update();

    switch (ledDriver.getPhase()) {
        case 1: // Red Read Window (500us)
            ppgFrame.redSum += analogReadMilliVolts(ADC_PIN);
            ppgFrame.redCount++;
            break;

        case 3: // Ambient 1 Read Window (500us)
            ppgFrame.amb1Sum += analogReadMilliVolts(ADC_PIN);
            ppgFrame.amb1Count++;
            break;

        case 5: // IR Read Window (500us)
            ppgFrame.irSum += analogReadMilliVolts(ADC_PIN);
            ppgFrame.irCount++;
            break;

        case 7: // Ambient 2 Read Window (500us)
            ppgFrame.amb2Sum += analogReadMilliVolts(ADC_PIN);
            ppgFrame.amb2Count++;
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
    selectMuxChannel(MUX_CHANNEL_PPG);

    oxygenInit();
    oxygenSetCalibration(0.0f, -25.0f, 110.0f);

    Wire.begin(5, 6);
    Wire.setClock(400000);
    setupGUI();

    WiFi.mode(WIFI_OFF);
    btStop();

    ledDriver.begin();

    xTaskCreatePinnedToCore(displayCode, "Display", 4096, nullptr, 1, &taskDisplay, 0);
}

void loop() {
    processPpgChannel();
    maybeServiceTelemetry();
}
