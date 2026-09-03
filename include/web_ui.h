#pragma once

#include <Arduino.h>

// On ESP32/ESP8266 builds, hosts a WiFi access point and serves a tiny
// status page: one box per Raceband channel, green when free / red when
// busy, polling the bitmask set via setBusyMask(). On boards without WiFi
// (Nano/Uno/Pro Micro), all three functions are no-ops so main.cpp doesn't
// need per-board #ifdefs.
namespace WebUi {
    // Starts the access point and HTTP server. Call once from setup().
    void begin();

    // Updates the busy bitmask the "/state" endpoint reports (bit i set =
    // channel i busy, matching the mask returned by updateChannelStateLeds()
    // in main.cpp).
    void setBusyMask(uint8_t busyMask);

    // Services pending HTTP requests. Call every loop() iteration.
    void handleClient();
}
