#ifndef CAP_ADC_H
#define CAP_ADC_H

#include "driver.h"

// =============================================================================
// CAPACITIVE DAC  (CDAC) – 74HC595 shift registers + TMUX1133/1112 switch array
// =============================================================================
//
// Architecture:
//   Two cascaded 74HC595s give 16 parallel outputs.
//   We use the lower 14 bits (Bit 0 = LSB, Bit 13 = MSB) to drive the
//   "bottom plates" of the binary-weighted capacitor array.
//   Each plate is toggled to either GND or V_REF (3.3 V) by the TMUX switches.
//
// SPI timing  (74HC595):
//   - Data is clocked in on the RISING edge of SHCP (SPI SCK).
//   - After all 16 bits are shifted, pulse STCP (PIN_CDAC_LATCH) HIGH then LOW
//     to transfer the shift register contents to the output latches.
//
// Bit weight (binary-weighted array):
//   Bit 13 (MSB) → sets ½ V_REF  at the summing node
//   Bit 12       → sets ¼ V_REF
//   …
//   Bit 0  (LSB) → sets V_REF / 2^14
// =============================================================================

/**
 * Write a 14-bit code (0–16383) to the CDAC shift registers.
 * Drives the bottom plates of the capacitor array to GND or V_REF.
 */
inline void setCdacVoltage(uint16_t value) {
    value &= 0x3FFF; // Clamp to 14 bits

    // Shift 16 bits into the two cascaded 74HC595s
    // (Upper 2 bits are unused / don't care)
    SPI.beginTransaction(SPISettings(10000000, MSBFIRST, SPI_MODE0));
    SPI.transfer16(value);
    SPI.endTransaction();

    // Pulse latch (STCP) to push shift register → output latches
    digitalWrite(PIN_CDAC_LATCH, HIGH);
    delayMicroseconds(1); // Hold time for 595 latch (tSU ≈ 25 ns, 1 µs is plenty)
    digitalWrite(PIN_CDAC_LATCH, LOW);
}

// =============================================================================
// 14-BIT SAR ALGORITHM  (uses CDAC + external comparator)
// =============================================================================
//
// Identical flow to the OTS SAR but operates over 14 bits and drives the
// capacitor array instead of the AD5621.
//
// Returns a 14-bit code proportional to the voltage at V_SMP.
// =============================================================================
inline uint16_t performSARCDAC() {
    uint16_t result = 0;

    // -- Step 1: Freeze the sample --
    setHoldMode();
    delayMicroseconds(1); // Allow the S/H switch to fully open

    // -- Step 2: 14-bit binary search --
    for (int bit = 13; bit >= 0; bit--) {
        uint16_t testVal = result | (1u << bit);

        setCdacVoltage(testVal);
        delayMicroseconds(10); // Wait for CDAC / switch settling

        if (digitalRead(PIN_COMP_OUT) == HIGH) {
            // V_SMP is still above the CDAC output → keep this bit
            result = testVal;
        }
        // Otherwise V_SMP was below CDAC → bit remains 0
    }

    // -- Step 3: Return to tracking for next sample --
    setTrackMode();

    return result; // 14-bit result, range 0–16383
}

#endif // CAP_ADC_H