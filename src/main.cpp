#include <Arduino.h>

#if defined(ARDUINO_ARCH_ESP32)
#include <driver/gpio.h>
#endif

#include "rx5808.h"
#include "tv_ui.h"
#include "web_ui.h"

// Time to let the PLL relock and the RSSI output settle after switching
// channels, before it can be trusted. RX5808/RTC6715 modules typically need
// ~20-30ms; lower it at your own risk (noisier readings) if you need a
// faster sweep across all 8 channels.
#define RSSI_SETTLE_MS 20

// Number of analogRead() samples averaged per channel per sweep.
#define RSSI_SAMPLES 8

// Raw ADC calibration range (0-1023) used only to derive the printed
// percentage. These are rough defaults - for accurate readings, log the raw
// values with your antenna both unplugged (noise floor) and fed a strong
// signal, then update these two constants.
#define RSSI_RAW_MIN 90
#define RSSI_RAW_MAX 280

// RSSI percentage at/above which a channel is considered busy (someone
// appears to be transmitting on it).
#define CHANNEL_BUSY_THRESHOLD_PCT 50

// One digital output per Raceband channel (R1..R8), each driving its own
// busy/free LED. Order matches RACEBAND_FREQUENCIES_MHZ / RACEBAND_CHANNEL_NAMES.
// Kept off the SPI bus pins (D10-D12) and the RSSI input (A0).
//
// Overridable via build_flags (e.g.
// -D CHANNEL_STATE_LED_PINS_LIST=19,21,22,23,25,26,27,32) for boards like
// the ESP32 where the AVR defaults land on pins reserved for other uses.
//
// The ESP8266 doesn't have eight GPIOs to spare for these on top of the
// RX5808 bus and WiFi (several are boot-mode strapping pins), so that board
// skips the LEDs entirely - see CHANNEL_STATE_LEDS_ENABLED below - and
// relies on the Web UI as its only busy/free indicator.
#if !defined(CHANNEL_STATE_LEDS_ENABLED)
#if defined(ARDUINO_ARCH_ESP8266)
#define CHANNEL_STATE_LEDS_ENABLED 0
#else
#define CHANNEL_STATE_LEDS_ENABLED 1
#endif
#endif

#if CHANNEL_STATE_LEDS_ENABLED
#ifndef CHANNEL_STATE_LED_PINS_LIST
#define CHANNEL_STATE_LED_PINS_LIST 2, 3, 4, 5, 6, 7, 8, 9
#endif
#endif

namespace {
#if CHANNEL_STATE_LEDS_ENABLED
    constexpr uint8_t CHANNEL_STATE_LED_PINS[RACEBAND_CHANNEL_COUNT] = {
        CHANNEL_STATE_LED_PINS_LIST,
    };
#endif

    uint16_t rssiRaw[RACEBAND_CHANNEL_COUNT];

    uint8_t toPercent(uint16_t raw) {
        long pct = map(raw, RSSI_RAW_MIN, RSSI_RAW_MAX, 0, 100);
        return static_cast<uint8_t>(constrain(pct, 0, 100));
    }

    void scanAllChannels() {
        for (uint8_t i = 0; i < RACEBAND_CHANNEL_COUNT; i++) {
            Rx5808::setFrequency(RACEBAND_FREQUENCIES_MHZ[i]);
            delay(RSSI_SETTLE_MS);
            rssiRaw[i] = Rx5808::readRssiRaw(RSSI_SAMPLES);
        }
    }

    // Lights each channel's LED (where CHANNEL_STATE_LEDS_ENABLED) according
    // to its own RSSI threshold and returns a per-channel busy bitmask (bit
    // i set = channel i busy) for reporting.
    uint8_t updateChannelStateLeds() {
        uint8_t busyMask = 0;
        for (uint8_t i = 0; i < RACEBAND_CHANNEL_COUNT; i++) {
            bool busy = toPercent(rssiRaw[i]) >= CHANNEL_BUSY_THRESHOLD_PCT;
#if CHANNEL_STATE_LEDS_ENABLED
            digitalWrite(CHANNEL_STATE_LED_PINS[i], busy ? HIGH : LOW);
#endif
            if (busy) {
                busyMask |= (1 << i);
            }
        }
        return busyMask;
    }

    void reportSweep(uint8_t busyMask) {
        Serial.print(millis());
        Serial.print(F(" ms\t"));
        for (uint8_t i = 0; i < RACEBAND_CHANNEL_COUNT; i++) {
            bool busy = busyMask & (1 << i);
            Serial.print(RACEBAND_CHANNEL_NAMES[i]);
            Serial.print('=');
            Serial.print(rssiRaw[i]);
            Serial.print(F(" ("));
            Serial.print(toPercent(rssiRaw[i]));
            Serial.print(F("%, "));
            Serial.print(busy ? F("BUSY") : F("FREE"));
            Serial.print(F(")"));
            if (i < RACEBAND_CHANNEL_COUNT - 1) {
                Serial.print('\t');
            }
        }
        Serial.println();
    }
}

void setup() {
    Serial.begin(SERIAL_BAUD);

    Rx5808::begin();

#if CHANNEL_STATE_LEDS_ENABLED
    for (uint8_t i = 0; i < RACEBAND_CHANNEL_COUNT; i++) {
        pinMode(CHANNEL_STATE_LED_PINS[i], OUTPUT);
        digitalWrite(CHANNEL_STATE_LED_PINS[i], LOW);
#if defined(ARDUINO_ARCH_ESP32)
        // Each LED only draws ~6mA (see README), so the ~10mA drive
        // setting covers it with a bit of margin, well under the 20mA
        // default.
        gpio_set_drive_capability(static_cast<gpio_num_t>(CHANNEL_STATE_LED_PINS[i]), GPIO_DRIVE_CAP_1);
#endif
    }
#endif

    Serial.println(F("Raceband RSSI scanner"));
    Serial.print(F("Channels:"));
    for (uint8_t i = 0; i < RACEBAND_CHANNEL_COUNT; i++) {
        Serial.print(' ');
        Serial.print(RACEBAND_CHANNEL_NAMES[i]);
        Serial.print('=');
        Serial.print(RACEBAND_FREQUENCIES_MHZ[i]);
    }
    Serial.println(F(" MHz"));

    WebUi::begin();
    TvUi::begin();
}

void loop() {
    scanAllChannels();

    uint8_t busyMask = updateChannelStateLeds();
    WebUi::setBusyMask(busyMask);
    TvUi::setBusyMask(busyMask);

    reportSweep(busyMask);

    WebUi::handleClient();
}
