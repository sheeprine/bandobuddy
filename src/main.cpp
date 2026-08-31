#include <Arduino.h>

#include "rx5808.h"

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

namespace {
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

    void reportSweep() {
        Serial.print(millis());
        Serial.print(F(" ms\t"));
        for (uint8_t i = 0; i < RACEBAND_CHANNEL_COUNT; i++) {
            Serial.print(RACEBAND_CHANNEL_NAMES[i]);
            Serial.print('=');
            Serial.print(rssiRaw[i]);
            Serial.print(F(" ("));
            Serial.print(toPercent(rssiRaw[i]));
            Serial.print(F("%)"));
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

    Serial.println(F("Raceband RSSI scanner"));
    Serial.print(F("Channels:"));
    for (uint8_t i = 0; i < RACEBAND_CHANNEL_COUNT; i++) {
        Serial.print(' ');
        Serial.print(RACEBAND_CHANNEL_NAMES[i]);
        Serial.print('=');
        Serial.print(RACEBAND_FREQUENCIES_MHZ[i]);
    }
    Serial.println(F(" MHz"));
}

void loop() {
    scanAllChannels();
    reportSweep();
}
