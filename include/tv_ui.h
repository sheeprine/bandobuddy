#pragma once

#include <Arduino.h>

// On AVR builds with ENABLE_TV_OUT defined, drives a composite PAL/NTSC video
// signal (via the TVout library) that mirrors the Web UI's channel grid: one
// box per Raceband channel, its name, and its free/busy state. Meant for
// wiring straight into a small CRT, composite monitor, or FPV goggles - no
// WiFi/browser needed. Composite video only carries luma, so unlike the Web
// UI there's no red/green coding; busy channels are instead marked with a
// double border in addition to the "BUSY"/"FREE" text, so the state doesn't
// depend on color here either.
//
// TVout hard-codes its video/sync pins per MCU (D7/D9 on ATmega328), which
// collide with two of this project's busy/free LEDs - see README for the
// pin remap that build environments enabling this feature need.
//
// No-op on any build without ENABLE_TV_OUT, so main.cpp doesn't need
// per-board #ifdefs (matching WebUi's pattern for non-WiFi boards).
namespace TvUi {
    // Allocates the frame buffer and starts video generation. Call once from
    // setup(). Safe to call even if the frame buffer can't be allocated -
    // logs to Serial and leaves the display blank rather than the sketch
    // failing.
    void begin();

    // Redraws the channel grid for the given busy bitmask (bit i set =
    // channel i busy, matching the mask returned by
    // updateChannelStateLeds() in main.cpp).
    void setBusyMask(uint8_t busyMask);
}
