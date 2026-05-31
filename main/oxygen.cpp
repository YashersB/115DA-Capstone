#include "oxygen.h"
#include <math.h>

#define BUFFER_SIZE 100 // Buffer of completed red/IR frame samples (5 seconds at 20Hz).

static float redAcBuffer[BUFFER_SIZE];
static float redDcBuffer[BUFFER_SIZE];
static float irAcBuffer[BUFFER_SIZE];
static float irDcBuffer[BUFFER_SIZE];

static uint16_t bufferIdx = 0;
static bool isBufferFull = false;

// Defaults match the current linear estimate until you calibrate with hardware.
static float cal_a = 0.0f;
static float cal_b = -25.0f;
static float cal_c = 110.0f;

void oxygenSetCalibration(float a, float b, float c) {
    cal_a = a;
    cal_b = b;
    cal_c = c;
}

void oxygenInit() {
    bufferIdx = 0;
    isBufferFull = false;

    for (int i = 0; i < BUFFER_SIZE; i++) {
        redAcBuffer[i] = 0.0f;
        redDcBuffer[i] = 0.0f;
        irAcBuffer[i] = 0.0f;
        irDcBuffer[i] = 0.0f;
    }
}

void oxygenAddSample(float redAC, float redDC, float irAC, float irDC) {
    redAcBuffer[bufferIdx] = redAC;
    redDcBuffer[bufferIdx] = redDC;
    irAcBuffer[bufferIdx] = irAC;
    irDcBuffer[bufferIdx] = irDC;
    bufferIdx++;

    if (bufferIdx >= BUFFER_SIZE) {
        bufferIdx = 0;
        isBufferFull = true;
    }
}

bool oxygenReady() {
    return isBufferFull;
}

static float calcMean(const float *buffer, int size) {
    float sum = 0.0f;

    for (int i = 0; i < size; i++) {
        sum += buffer[i];
    }

    return sum / size;
}

static float calcRMS_ac(const float *buffer, float dc, int size) {
    float sumSquares = 0.0f;

    for (int i = 0; i < size; i++) {
        float ac = buffer[i] - dc;
        sumSquares += ac * ac;
    }

    return sqrt(sumSquares / size);
}

spo2calc oxygencompute() {
    spo2calc result;

    // Hardware DC provides baseline
    result.dcRed = calcMean(redDcBuffer, BUFFER_SIZE);
    result.dcIR = calcMean(irDcBuffer, BUFFER_SIZE);
    
    // Hardware AC provides heartbeat waveform (calcRMS_ac automatically centers it)
    result.acRed = calcRMS_ac(redAcBuffer, calcMean(redAcBuffer, BUFFER_SIZE), BUFFER_SIZE);
    result.acIR = calcRMS_ac(irAcBuffer, calcMean(irAcBuffer, BUFFER_SIZE), BUFFER_SIZE);

    result.valid = false;
    result.ratio = 0.0f;
    result.spo2 = 0.0f;

    if (result.dcRed <= 0.0f || result.dcIR <= 0.0f ||
        result.acRed <= 0.0f || result.acIR <= 0.0f) {
        return result;
    }

    float redRatio = result.acRed / result.dcRed;
    float irRatio = result.acIR / result.dcIR;

    if (irRatio <= 0.0f) {
        return result;
    }

    result.ratio = redRatio / irRatio;
    result.spo2 = (cal_a * result.ratio * result.ratio) + (cal_b * result.ratio) + cal_c;

    if (result.spo2 > 100.0f) result.spo2 = 100.0f;
    if (result.spo2 < 0.0f) result.spo2 = 0.0f;

    result.valid = true;
    return result;
}
