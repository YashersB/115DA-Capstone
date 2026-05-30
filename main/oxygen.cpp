#include "oxygen.h"

#define BUFFER_SIZE 400 

static float redBuffer[BUFFER_SIZE];
static float irBuffer[BUFFER_SIZE];

static uint16_t bufferIdx = 0;
static bool isBufferFull = false;

void oxygenInit() {
    bufferIdx = 0;
    isBufferFull = false;
    // Clear out any old garbage data
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

static float calcMean(float *buffer, int size) {
    float sum = 0.0f;
    for(int i = 0; i < size; i++) {
        sum += buffer[i];
    }
    return sum / size;
}

static float calcRMS_ac(float *buffer, float dc, int size) {
    float sumSquares = 0.0f;
    for (int i = 0; i < size; i++) {
        float ac = buffer[i] - dc;
        sumSquares += ac * ac;
    }
    return sqrt(sumSquares / size);
}
 
spo2calc oxygencompute() {
    spo2calc result;

    // Calc DC components
    result.dcRed = calcMean(redBuffer, BUFFER_SIZE);
    result.dcIR = calcMean(irBuffer, BUFFER_SIZE);

    // Calc AC (RMS) components
    result.acRed = calcRMS_ac(redBuffer, result.dcRed, BUFFER_SIZE);
    result.acIR = calcRMS_ac(irBuffer, result.dcIR, BUFFER_SIZE);

    result.valid = false;
    result.ratio = 0.0f;
    result.spo2  = 0.0f;

    // Check for invalid data (prevents dividing by zero)
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

    // Linear approximation of R curve
    result.spo2 = 110.0f - 25.0f * result.ratio;

    // Clamp to realistic bounds
    if (result.spo2 > 100.0f) result.spo2 = 100.0f;
    if (result.spo2 < 0.0f)   result.spo2 = 0.0f;

    result.valid = true;
    return result; 
}