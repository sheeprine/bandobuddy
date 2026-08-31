#pragma once

#include <Arduino.h>

// ---------------------------------------------------------------------------
// Wiring
//
// The RX5808 (and RX5808-compatible RTC6715-based modules, including a
// modified RC832) exposes a 3-wire bit-banged bus once the SPI mod has been
// applied (the SPI_CLK/SPI_DATA/SPI_LE test points are bridged/unlocked by
// removing the onboard bus-select resistor). Wire it as follows:
//
//   RX5808 pad      Arduino pin
//   ----------      -----------
//   DATA            RX5808_PIN_DATA
//   CLK             RX5808_PIN_CLOCK
//   LE / SEL (CS)   RX5808_PIN_LE
//   RSSI            RX5808_PIN_RSSI (analog input)
//   3.3V / GND      per module regulator (RX5808 logic is 3.3V tolerant on
//                    most pins, but check your specific board revision)
// ---------------------------------------------------------------------------
// Overridable via build_flags (e.g. -D RX5808_PIN_DATA=14) for boards like
// the Arduino Pro Micro that don't break out D11/D12 on their header.
#ifndef RX5808_PIN_DATA
#define RX5808_PIN_DATA  10
#endif
#ifndef RX5808_PIN_CLOCK
#define RX5808_PIN_CLOCK 11
#endif
#ifndef RX5808_PIN_LE
#define RX5808_PIN_LE    12
#endif
#ifndef RX5808_PIN_RSSI
#define RX5808_PIN_RSSI  A0
#endif

// Number of channels in the Raceband (Band E / "R") plate.
#define RACEBAND_CHANNEL_COUNT 8

// Raceband channel frequencies in MHz, R1..R8, in module/hardware order.
extern const uint16_t RACEBAND_FREQUENCIES_MHZ[RACEBAND_CHANNEL_COUNT];
extern const char *const RACEBAND_CHANNEL_NAMES[RACEBAND_CHANNEL_COUNT];

namespace Rx5808 {
    // Configures the control/RSSI pins. Call once from setup().
    void begin();

    // Tunes the receiver's synthesizer to the given frequency (in MHz) by
    // bit-banging the 25-bit SPI register write (4-bit address + 1 R/W bit +
    // 20-bit data, LSB first). Address 0x1 is the Synthesizer Register B on
    // the RTC6715 used inside RX5808 modules.
    void setFrequency(uint16_t freqMhz);

    // Reads the analog RSSI pin. Performs a throwaway read first to let the
    // ADC input mux settle after the previous analogRead() on a different
    // pin/channel, then averages `samples` readings.
    uint16_t readRssiRaw(uint8_t samples = 8);
}
