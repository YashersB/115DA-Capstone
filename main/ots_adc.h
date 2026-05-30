#ifndef OTS_ADC_H
#define OTS_ADC_H

#include "driver.h"

// =============================================================================
// OFF-THE-SHELF DAC  –  Analog Devices AD5621 (12-bit, SPI)
// =============================================================================
//
// 16-bit SPI frame layout:
//   Bit 15-14 : Power-Down bits  (00 = Normal operation)
//   Bit 13-2  : 12-bit data      (D11 … D0)
//   Bit 1-0   : Don't care
//
// So the 12-bit value is left-shifted by 2 inside the 16-bit frame.
// =============================================================================

/**
 * Write a 12-bit code (0–4095) to the AD5621 DAC over SPI.
 * Output voltage ≈ (value / 4095) × V_REF  (V_REF = 3.3 V on your board).
 */
inline void setOtsDacVoltage(uint16_t value) {
    value &= 0x0FFF;                    // Clamp to 12 bits
    uint16_t frame = (value << 2);      // Align to bits [13:2], PD bits = 00

    SPI.beginTransaction(SPISettings(10000000, MSBFIRST, SPI_MODE1));
    digitalWrite(PIN_DAC_CS, LOW);      // Assert CS
    SPI.transfer16(frame);
    digitalWrite(PIN_DAC_CS, HIGH);     // Deassert CS – DAC latches on rising edge
    SPI.endTransaction();
}

// =============================================================================
// 12-BIT SAR ALGORITHM  (uses OTS DAC + external comparator)
// =============================================================================
//
// Algorithm per conversion:
//   1. Drop LATCH LOW → Hold Mode  (freeze V_SMP on 1 nF cap)
//   2. Binary search over 12 bits (MSB first):
//      a. Output test voltage to AD5621
//      b. Wait 10 µs for DAC settling
//      c. Read PIN_COMP_OUT
//         – HIGH  → V_SMP > DAC_OUT → keep bit SET
//         – LOW   → V_SMP < DAC_OUT → clear bit
//   3. Raise LATCH HIGH → Track Mode  (ready for next sample)
//
// Returns a 12-bit code proportional to the voltage at V_SMP.
// =============================================================================
inline uint16_t performSAROffTheShelf() {
    uint16_t result = 0;

    // -- Step 1: Freeze the sample --
    setHoldMode();
    delayMicroseconds(1); // Allow the S/H switch to fully open

    // -- Step 2: 12-bit binary search --
    for (int bit = 11; bit >= 0; bit--) {
        uint16_t testVal = result | (1u << bit);

        setOtsDacVoltage(testVal);
        delayMicroseconds(10); // Wait for DAC output to settle

        if (digitalRead(PIN_COMP_OUT) == HIGH) {
            // V_SMP is still above the DAC → keep this bit
            result = testVal;
        }
        // Otherwise V_SMP was below the DAC → bit remains 0
    }

    // -- Step 3: Return to tracking for next sample --
    setTrackMode();

    return result; // 12-bit result, range 0–4095
}

#endif // OTS_ADC_H