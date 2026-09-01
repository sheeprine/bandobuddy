#pragma once

#include <Arduino.h>

// With ENABLE_TV_OUT defined, drives a composite PAL/NTSC video signal that
// mirrors the Web UI's channel grid: one box per Raceband channel, its
// name, and its free/busy state. Meant for wiring straight into a small
// CRT, composite monitor, or FPV goggles - no WiFi/browser needed.
//
// Two independent backends share this same API, picked by architecture at
// compile time (see tv_ui.cpp):
//   - AVR (Uno/Nano/Pro Micro): the TVout library. Composite video only
//     carries luma there, so unlike the Web UI there's no red/green coding;
//     busy channels are instead marked with a solid block in addition to
//     the "BUSY"/"FREE" text, so the state doesn't depend on color either.
//   - ESP32: the ESP_8_BIT library, off the onboard DAC. This one *does*
//     carry color, so it uses the same green/red coding as the Web UI.
//
// Both hard-code their video pin(s) per chip, colliding with some of this
// project's busy/free LEDs - see README for the pin remap each build
// environment enabling this feature needs.
//
// No-op on any build without ENABLE_TV_OUT, so main.cpp doesn't need
// per-board #ifdefs (matching WebUi's pattern for non-WiFi boards).
namespace TvUi {
    // Allocates the frame buffer and starts video generation. Call once
    // from setup(). On AVR, safe to call even if the frame buffer can't be
    // allocated - logs to Serial and leaves the display blank rather than
    // the sketch failing.
    void begin();

    // Redraws the channel grid for the given busy bitmask (bit i set =
    // channel i busy, matching the mask returned by
    // updateChannelStateLeds() in main.cpp).
    void setBusyMask(uint8_t busyMask);
}
