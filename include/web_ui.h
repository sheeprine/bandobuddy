#pragma once

#include <Arduino.h>

// On ESP32/ESP8266 builds, hosts a WiFi access point and serves a tiny
// status page: one box per Raceband channel, green when free / red when
// busy, polling the bitmask set via setBusyMask(). It also serves an
// "/admin" page for viewing/editing the RSSI calibration and busy
// threshold (see calibration.h) at runtime, including a guided two-step
// "Calibrate" wizard that reads live RSSI via setRssiRaw(). On boards
// without WiFi (Nano/Uno/Pro Micro), all functions are no-ops so main.cpp
// doesn't need per-board #ifdefs.
namespace WebUi {
    // Starts the access point and HTTP server. Call once from setup().
    void begin();

    // Updates the busy bitmask the "/state" endpoint reports (bit i set =
    // channel i busy, matching the mask returned by updateChannelStateLeds()
    // in main.cpp).
    void setBusyMask(uint8_t busyMask);

    // Updates the raw RSSI readings the "/rssi" endpoint reports (used by
    // the admin page's live calibration wizard). rssiRaw must point to
    // RACEBAND_CHANNEL_COUNT values, in the same channel order as
    // RACEBAND_CHANNEL_NAMES.
    void setRssiRaw(const uint16_t *rssiRaw);

    // Services pending HTTP requests. Call every loop() iteration.
    void handleClient();
}
