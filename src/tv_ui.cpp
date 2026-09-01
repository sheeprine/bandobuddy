#include "tv_ui.h"

#if defined(ENABLE_TV_OUT)

#include <TVout.h>
#include <font6x8.h>

#include "rx5808.h"

// 128x64 rather than TVout's 128x96 default: the frame buffer is
// hres/8 * vres bytes (1024 here vs. 1536), which matters a lot on a 2KB
// ATmega328 once the rest of the sketch's globals and stack are accounted
// for. Must stay a multiple of 8 (TVout::begin() requirement).
#define TV_HRES 128
#define TV_VRES 64

#define GRID_COLS 4
#define CELL_W (TV_HRES / GRID_COLS)
#define CELL_H (TV_VRES / (RACEBAND_CHANNEL_COUNT / GRID_COLS))

namespace {
    TVout tv;
    bool ready = false;

    void drawChannel(uint8_t i, bool busy) {
        uint8_t x0 = (i % GRID_COLS) * CELL_W;
        uint8_t y0 = (i / GRID_COLS) * CELL_H;

        // Clear the cell, then an inset outline border for every channel.
        tv.draw_rect(x0, y0, CELL_W - 1, CELL_H - 1, BLACK, BLACK);
        tv.draw_rect(x0 + 1, y0 + 1, CELL_W - 3, CELL_H - 3, WHITE, -1);

        // A solid marker block, not just the text, so the busy/free
        // distinction still reads at a glance (see tv_ui.h - no color to
        // lean on here like the Web UI has).
        if (busy) {
            tv.draw_rect(x0 + CELL_W - 10, y0 + 3, 6, 6, WHITE, WHITE);
        }

        tv.print(x0 + 4, y0 + 4, RACEBAND_CHANNEL_NAMES[i]);
        tv.print(x0 + 4, y0 + 18, busy ? "BUSY" : "FREE");
    }
}

namespace TvUi {
    void begin() {
#if defined(TV_OUT_PAL)
        uint8_t mode = PAL;
#else
        uint8_t mode = NTSC;
#endif
        ready = tv.begin(mode, TV_HRES, TV_VRES) == 0;
        if (!ready) {
            Serial.println(F("TV out: not enough RAM for the frame buffer, disabled"));
            return;
        }

        tv.select_font(font6x8);
        for (uint8_t i = 0; i < RACEBAND_CHANNEL_COUNT; i++) {
            drawChannel(i, false);
        }
    }

    void setBusyMask(uint8_t busyMask) {
        if (!ready) {
            return;
        }
        for (uint8_t i = 0; i < RACEBAND_CHANNEL_COUNT; i++) {
            drawChannel(i, busyMask & (1 << i));
        }
    }
}

#else

namespace TvUi {
    void begin() {}
    void setBusyMask(uint8_t) {}
}

#endif
