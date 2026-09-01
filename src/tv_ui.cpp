#include "tv_ui.h"

#if defined(ENABLE_TV_OUT) && defined(ARDUINO_ARCH_ESP32)

#include <ESP_8_BIT_GFX.h>

#include "rx5808.h"

// 256x240 frame buffer (4:3, non-square pixels - see ESP_8_BIT_GFX.h),
// split into the same 4-column, 2-row grid as the Web UI and the AVR
// TVout build.
#define GRID_COLS 4
#define CELL_W (256 / GRID_COLS)
#define CELL_H (240 / (RACEBAND_CHANNEL_COUNT / GRID_COLS))

// RGB332 - this library's native color depth (see ESP_8_BIT_GFX.h).
#define COLOR_BLACK 0x00
#define COLOR_WHITE 0xFF
#define COLOR_GREEN 0x1C
#define COLOR_RED   0xE0

#if defined(TV_OUT_PAL)
#define TV_OUT_NTSC false
#else
#define TV_OUT_NTSC true
#endif

namespace {
    ESP_8_BIT_GFX tv(TV_OUT_NTSC, 8);

    void drawChannel(uint8_t i, bool busy) {
        int16_t x0 = (i % GRID_COLS) * CELL_W;
        int16_t y0 = (i / GRID_COLS) * CELL_H;

        // Unlike the AVR TVout build, this library's composite signal does
        // carry color, so busy/free can use the same green/red coding as
        // the Web UI rather than a shape-only marker.
        tv.fillRect(x0 + 2, y0 + 2, CELL_W - 4, CELL_H - 4, busy ? COLOR_RED : COLOR_GREEN);

        tv.setTextColor(COLOR_WHITE);
        tv.setTextSize(2);
        tv.setCursor(x0 + 8, y0 + 10);
        tv.print(RACEBAND_CHANNEL_NAMES[i]);

        tv.setTextSize(1);
        tv.setCursor(x0 + 8, y0 + 40);
        tv.print(busy ? "BUSY" : "FREE");
    }
}

namespace TvUi {
    void begin() {
        tv.begin();
        tv.fillScreen(COLOR_BLACK);
        for (uint8_t i = 0; i < RACEBAND_CHANNEL_COUNT; i++) {
            drawChannel(i, false);
        }
    }

    void setBusyMask(uint8_t busyMask) {
        for (uint8_t i = 0; i < RACEBAND_CHANNEL_COUNT; i++) {
            drawChannel(i, busyMask & (1 << i));
        }
    }
}

#elif defined(ENABLE_TV_OUT)

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
