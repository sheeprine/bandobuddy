#include "calibration.h"

namespace {
    uint16_t currentRssiMin = RSSI_RAW_MIN_DEFAULT;
    uint16_t currentRssiMax = RSSI_RAW_MAX_DEFAULT;
    uint8_t currentBusyThresholdPct = CHANNEL_BUSY_THRESHOLD_PCT_DEFAULT;
}

namespace Calibration {
    uint16_t rssiMin() {
        return currentRssiMin;
    }

    uint16_t rssiMax() {
        return currentRssiMax;
    }

    uint8_t busyThresholdPct() {
        return currentBusyThresholdPct;
    }

    bool set(uint16_t rssiMin, uint16_t rssiMax, uint8_t busyThresholdPct) {
        if (rssiMin >= rssiMax || busyThresholdPct > 100) {
            return false;
        }
        currentRssiMin = rssiMin;
        currentRssiMax = rssiMax;
        currentBusyThresholdPct = busyThresholdPct;
        return true;
    }
}
