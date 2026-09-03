#pragma once

#include <Arduino.h>

// Runtime-adjustable RSSI-to-percentage calibration and busy threshold.
// Seeded from compile-time defaults below (still overridable via
// build_flags, e.g. -D RSSI_RAW_MIN_DEFAULT=100, for a board-specific
// starting point), but changeable afterwards without reflashing - see the
// Web UI's admin page (esp32/esp8266 builds) and its "Calibrate" wizard.
// On boards without the Web UI (Nano/Uno/Pro Micro), these values never
// change at runtime, so behavior there is identical to the old compile-time
// constants.
#ifndef RSSI_RAW_MIN_DEFAULT
#define RSSI_RAW_MIN_DEFAULT 90
#endif
#ifndef RSSI_RAW_MAX_DEFAULT
#define RSSI_RAW_MAX_DEFAULT 280
#endif
#ifndef CHANNEL_BUSY_THRESHOLD_PCT_DEFAULT
#define CHANNEL_BUSY_THRESHOLD_PCT_DEFAULT 50
#endif

namespace Calibration {
    // Raw ADC value considered the noise floor (maps to 0%).
    uint16_t rssiMin();

    // Raw ADC value considered a strong signal (maps to 100%).
    uint16_t rssiMax();

    // RSSI percentage at/above which a channel is considered busy.
    uint8_t busyThresholdPct();

    // Updates all three values at once. Rejects (returns false, leaving the
    // current values untouched) an inverted/degenerate range (min >= max)
    // or an out-of-range threshold (> 100).
    bool set(uint16_t rssiMin, uint16_t rssiMax, uint8_t busyThresholdPct);
}
