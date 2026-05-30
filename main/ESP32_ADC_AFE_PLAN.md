# ESP32 ADC Integration Plan

## Goal

Use the ESP32 internal ADC plus the analog mux to validate the transmissive LED driver, analog front end, ambient subtraction, GUI waveform path, and SpO2 math before the discrete SAR ADC is ready.

This plan keeps the optical channel running continuously and treats PTAT and other muxed analog nodes as slow side reads instead of long mode switches.

## Channel Map

- `MUX channel 1`: Primary PPG / analog front-end output
- `MUX channel 2`: Reserved for future BPM tap or alternate conditioned waveform
- `MUX channel 3`: PTAT temperature reference
- `MUX channel 4`: Reserved for future analog debug node
- `ADC_BAT`: Battery monitor on its own ADC pin, not through the mux

## Core Timing Model

Your LED driver uses an 8-phase frame:

1. Phase `0`: all off, start-of-frame idle
2. Phase `1`: red read window
3. Phase `2`: all off settle before ambient 1
4. Phase `3`: ambient 1 read window
5. Phase `4`: IR settle
6. Phase `5`: IR read window
7. Phase `6`: all off settle before ambient 2
8. Phase `7`: ambient 2 read window

With `settleTimeUS = 2000` and `readTimeUS = 500`, one full frame is:

- `4 x 2000 us` settle windows = `8000 us`
- `4 x 500 us` read windows = `2000 us`
- Total frame time = `10000 us`
- Effective PPG frame rate = about `100 Hz`

That is a good working rate for oversampled pulse oximetry development.

## Acquisition Strategy

### PPG Path

The PPG path should own the mux almost all the time.

During each frame:

- Phase `1`: accumulate `red + ambient`
- Phase `3`: accumulate `ambient 1`
- Phase `5`: accumulate `IR + ambient`
- Phase `7`: accumulate `ambient 2`

When the driver returns to phase `0`, compute:

- `trueRed = avg(red + ambient) - avg(ambient 1)`
- `trueIR = avg(IR + ambient) - avg(ambient 2)`

Then:

- clamp negatives to zero
- push `trueRed` and `trueIR` into `oxygenAddSample()`
- decimate `trueIR` into the GUI waveform buffer

## Why Not Flip the Mux Every 2 Seconds

A long toggle between PPG and PTAT breaks continuity:

- the oxygen buffer gets gaps instead of a steady waveform
- BPM timing becomes unreliable later
- the OLED waveform appears frozen or discontinuous
- the LED state machine loses its clean frame-to-frame meaning

Instead, keep channel 1 active continuously and only steal a short read from channel 3 when needed.

## Slow Analog Reads

PTAT and battery are slow-changing signals, so they do not need continuous ownership of the ADC path.

Recommended behavior:

- every `500 ms`, mark PTAT as due
- wait for `phase 0`
- briefly switch mux to PTAT
- take a short oversampled burst
- read battery on `ADC_BAT` during the same service window
- switch mux back to PPG
- reset the LED frame so the next optical cycle starts cleanly

This produces only a tiny PPG interruption instead of a multi-second blind spot.

## Software Responsibilities

### `LEDDriver.h`

- owns optical excitation timing
- defines the exact read windows
- should restart from phase `0` after any non-PPG service burst

### `main.ino`

- owns mux routing
- owns ADC reads
- owns oversampling accumulators
- performs ambient subtraction
- schedules slow telemetry reads only at safe frame boundaries
- feeds the GUI with the recovered IR waveform

### `oxygen.cpp`

- stores completed red/IR frame samples
- extracts `DC` with a mean
- extracts `AC` with RMS around the mean
- computes ratio-of-ratios
- applies the current polynomial calibration

### `gui.h`

- displays `SpO2`, `BPM`, `temp`, and battery
- plots the decimated `trueIR` waveform

## Buffer and Latency Notes

`oxygen.cpp` currently uses a `BUFFER_SIZE` of `400`.

At roughly `100 Hz`, that means:

- about `4 seconds` to fill the first buffer
- first valid SpO2 estimate after about `4 seconds`

That is fine for bring-up. If you need a faster first reading later, reduce the buffer size or move to a rolling-window compute.

## Test Plan

### 1. Oversampling Count Check

Add a debug print for the per-window sample counts.

Expected result:

- more than `1` sample per 500 us window
- preferably several samples per window, depending on `analogReadMilliVolts()` speed

### 2. Ambient Rejection Check

Plot `trueIR` and `trueRed` while moving ambient light nearby.

Expected result:

- baseline may shift slightly
- pulse waveform should remain visible
- ambient-only changes should be reduced compared to raw window values

### 3. AC/DC Sanity Check

Inspect:

- `dcRed`, `dcIR`
- `acRed`, `acIR`
- `ratio`

Expected result:

- `DC` stays comfortably above zero
- `AC` is small but non-zero
- `ratio` moves realistically with finger placement and contact quality

### 4. Telemetry Intrusion Check

While PTAT reads are enabled every `500 ms`, verify:

- PPG waveform does not collapse
- SpO2 still updates
- temperature and battery continue to refresh

## Recommended Next Steps

1. Keep channel 1 and channel 3 working first.
2. Decide what channel 2 should carry:
   separate BPM-conditioned path or duplicate debug node.
3. Once the discrete SAR path is ready, replace the `analogReadMilliVolts()` calls in the acquisition layer only.
4. Leave `oxygen.cpp`, GUI, and frame timing logic unchanged during that swap.

## Future SAR Handoff

When you are ready to move off the ESP32 ADC, the clean boundary is:

- current implementation: `analogReadMilliVolts(ADC_PIN)`
- future implementation: `performSAROffTheShelf()` or `performSARCDAC()`

Convert the SAR code to millivolts at the acquisition layer, then feed the same ambient-subtraction and oxygen pipeline.
