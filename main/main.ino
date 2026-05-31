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
constexpr uint8_t WAVEFORM_DECIMATION = 6;
constexpr bool ENABLE_PPG_DEBUG = true; // Set to true to see outputs on the terminal
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
        "SpO2: %.1f %% | BPM: %d | Temp: %.1f C | Bat: %d mV | Ratio: %.3f | trueRedAC: %.2f | trueIrAC: %.2f\n",
        result.spo2,
        currentBPM,
        tempC,
        batteryMilliVolts,
        result.ratio,
        trueRed, // Used as trueRedAC in the caller
        trueIR); // Used as trueIrAC in the caller

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

    if (trueRedDc < 0.0f) trueRedDc = 0.0f;
    if (trueIrDc < 0.0f) trueIrDc = 0.0f;

    oxygenAddSample(trueRedAc, trueRedDc, trueIrAc, trueIrDc);

    if (oxygenReady()) {
        spo2calc result = oxygencompute();
        if (result.valid) {
            currentSpO2 = result.spo2;
            maybePrintDebug(result, trueRedAc, trueIrAc);
        }
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

    xTaskCreatePinnedToCore(displayCode, "Display", 4096, nullptr, 1, &taskDisplay, 0);
}

void loop() {
    processPpgChannel();
    maybeServiceTelemetry();
}
