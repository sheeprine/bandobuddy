# Raceband RSSI Scanner

[![License: Apache 2.0](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](LICENSE)

Scans all 8 channels of the analog FPV Raceband (5658-5917 MHz) with a
single RX5808/RTC6715-based receiver module and reports RSSI per channel
over serial. Since one receiver can only listen to one frequency at a time,
"monitoring 8 channels" means continuously re-tuning the module across all
8 Raceband channels and sampling RSSI after each retune (a frequency sweep),
rather than listening to all 8 simultaneously.

## Hardware

Any RX5808 module works, including a modified RC832 with its RTC6715 tuner
chip's SPI control lines exposed (the standard "RX5808 SPI mod": bridging or
removing the onboard resistor/pad that ties the bus-select pin low, freeing
up DATA/CLK/LE for direct microcontroller control instead of the module's
onboard button-scan logic).

Wire the module to an Arduino Uno/Nano (ATmega328P) as follows:

| RX5808 / RC832 pad | Arduino pin      |
|---------------------|-----------------|
| DATA                 | D10             |
| CLK                  | D11             |
| LE / SEL (CS)        | D12             |
| RSSI                 | A0              |
| GND                  | GND             |
| 3.3V / 5V            | per your module's onboard regulator |

Pin assignments are `#define`s at the top of `include/rx5808.h` if you need
to change them.

Also wire one LED per Raceband channel (with a current-limiting resistor,
e.g. 220Ω) from each pin in `CHANNEL_STATE_LED_PINS` to GND:

| Channel | LED pin |
|---------|---------|
| R1      | D2      |
| R2      | D3      |
| R3      | D4      |
| R4      | D5      |
| R5      | D6      |
| R6      | D7      |
| R7      | D8      |
| R8      | D9      |

Each LED lights up whenever that specific channel is considered busy (see
below). Pins are defined in `src/main.cpp`.

## Building

```sh
pio run                       # build (defaults to nanoatmega328new)
pio run -t upload             # build + flash
pio device monitor -b 115200  # view RSSI output
```

Other board environments are defined in `platformio.ini`:
- `nanoatmega328new` - Nano with the newer bootloader (default)
- `nanoatmega328` - Nano with the old bootloader (uses 57600 baud upload)
- `uno` - Arduino Uno

Select one explicitly with `pio run -e uno`.

## Output

Each line is one full 8-channel sweep, printed as raw ADC RSSI (0-1023), a
calibrated percentage, and each channel's resulting busy/free state:

```
1234 ms	R1=142 (29%, FREE)	R2=138 (25%, FREE)	R3=205 (60%, BUSY)	R4=95 (2%, FREE)	R5=101 (5%, FREE)	R6=110 (10%, FREE)	R7=99 (4%, FREE)	R8=120 (15%, FREE)
```

## Per-channel busy/free LEDs

After every sweep, each channel's calibrated RSSI percentage is compared
against `CHANNEL_BUSY_THRESHOLD_PCT` (50% by default, in `src/main.cpp`).
Channels at or above the threshold are considered **busy** - a video
transmitter appears to be active on that frequency - and their LED is
driven HIGH. Channels below it are **free** and their LED is driven LOW.
This gives a quick visual per-channel "is this Raceband frequency clear"
indicator without needing the serial monitor.

Tune `CHANNEL_BUSY_THRESHOLD_PCT` to taste once you've calibrated
`RSSI_RAW_MIN`/`RSSI_RAW_MAX` for your module (see Calibration below) -
raise it to only flag strong/close transmitters, lower it to catch weak/far
ones too.

## Calibration

The percentage figure is derived from `RSSI_RAW_MIN`/`RSSI_RAW_MAX` in
`src/main.cpp`, which are rough defaults. To calibrate for your specific
module:

1. Flash and open the serial monitor with no video transmitter powered on
   nearby - note the raw values (noise floor) → use as `RSSI_RAW_MIN`.
2. Power a video transmitter at close range on each Raceband channel in
   turn and note the raw peak values → use the highest as `RSSI_RAW_MAX`.
3. Update the two constants and reflash.

## Tuning speed vs. stability

`RSSI_SETTLE_MS` in `src/main.cpp` controls how long the code waits after
switching channels before trusting the RSSI reading (PLL relock + RSSI
output settling time). The default of 20ms gives a full 8-channel sweep in
roughly 170-200ms (~5 sweeps/second). Lowering it speeds up the scan but
increases reading noise/inaccuracy right after a channel switch.
