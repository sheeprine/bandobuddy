#include "rx5808.h"

const uint16_t RACEBAND_FREQUENCIES_MHZ[RACEBAND_CHANNEL_COUNT] = {
    5658, 5695, 5732, 5769, 5806, 5843, 5880, 5917,
};

const char *const RACEBAND_CHANNEL_NAMES[RACEBAND_CHANNEL_COUNT] = {
    "R1", "R2", "R3", "R4", "R5", "R6", "R7", "R8",
};

namespace {
    constexpr uint8_t SPI_ADDRESS_SYNTH_B = 0x01;

    void sendBit(uint8_t value) {
        digitalWrite(RX5808_PIN_CLOCK, LOW);
        delayMicroseconds(1);

        digitalWrite(RX5808_PIN_DATA, value ? HIGH : LOW);
        delayMicroseconds(1);
        digitalWrite(RX5808_PIN_CLOCK, HIGH);
        delayMicroseconds(1);

        digitalWrite(RX5808_PIN_CLOCK, LOW);
        delayMicroseconds(1);
    }

    void sendBits(uint32_t bits, uint8_t count) {
        // LSB first.
        for (uint8_t i = 0; i < count; i++) {
            sendBit(bits & 0x1);
            bits >>= 1;
        }
    }

    // Converts a frequency in MHz into the 20-bit synthesizer register
    // payload (N and A divider ratios), per the RTC6715 datasheet:
    //   F_lo = 2 * (N * 32 + A) * (F_osc / R), with F_osc = 8MHz, R = 8
    uint32_t frequencyToSynthRegister(uint16_t freqMhz) {
        uint16_t tf = (freqMhz - 479) / 2;
        uint16_t n = tf / 32;
        uint16_t a = tf % 32;
        return (static_cast<uint32_t>(n) << 7) | a;
    }
}

namespace Rx5808 {
    void begin() {
        pinMode(RX5808_PIN_DATA, OUTPUT);
        pinMode(RX5808_PIN_CLOCK, OUTPUT);
        pinMode(RX5808_PIN_LE, OUTPUT);

        digitalWrite(RX5808_PIN_DATA, LOW);
        digitalWrite(RX5808_PIN_CLOCK, LOW);
        digitalWrite(RX5808_PIN_LE, HIGH);
    }

    void setFrequency(uint16_t freqMhz) {
        uint32_t data = frequencyToSynthRegister(freqMhz);

        digitalWrite(RX5808_PIN_LE, LOW);

        sendBits(SPI_ADDRESS_SYNTH_B, 4);
        sendBit(HIGH); // Register write enable.
        sendBits(data, 20);

        digitalWrite(RX5808_PIN_LE, HIGH);
        digitalWrite(RX5808_PIN_CLOCK, LOW);
        digitalWrite(RX5808_PIN_DATA, LOW);
    }

    uint16_t readRssiRaw(uint8_t samples) {
        analogRead(RX5808_PIN_RSSI); // Let the ADC mux settle.

        uint32_t total = 0;
        for (uint8_t i = 0; i < samples; i++) {
            total += analogRead(RX5808_PIN_RSSI);
        }
        return static_cast<uint16_t>(total / samples);
    }
}
