#include "oxygen.h"
#include <math.h>

#define BUFFER_SIZE 400 // Buffer of completed red/IR frame samples.

static float redBuffer[BUFFER_SIZE];
static float irBuffer[BUFFER_SIZE];

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
        redBuffer[i] = 0.0f;
        irBuffer[i] = 0.0f;
    }
}

void oxygenAddSample(float redSample, float irSample) {
    redBuffer[bufferIdx] = redSample;
    irBuffer[bufferIdx] = irSample;
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

    result.dcRed = calcMean(redBuffer, BUFFER_SIZE);
    result.dcIR = calcMean(irBuffer, BUFFER_SIZE);
    result.acRed = calcRMS_ac(redBuffer, result.dcRed, BUFFER_SIZE);
    result.acIR = calcRMS_ac(irBuffer, result.dcIR, BUFFER_SIZE);

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
