// Vendored from arduino-tvout's TVoutfonts/font6x8.h (MIT license, Myles
// Metzer / https://github.com/Avamander/arduino-tvout). The registry
// package (avamander/TVout) only compiles .cpp files in its own root, not
// its bundled TVoutfonts subfolder, so font6x8's data would otherwise never
// get linked in - see tv_ui.cpp for the font this pulls in.
#ifndef FONT6X8_H
#define FONT6X8_H

// AVR-only (PROGMEM/pgmspace): guarded so PlatformIO's Library Dependency
// Finder pulling this in for non-AVR environments (it scans #include lines
// textually, without regard to the ENABLE_TV_OUT preprocessor guard around
// tv_ui.cpp's own #include of this header) compiles to an empty TU instead
// of a toolchain error.
#if defined(__AVR__)

#include <avr/pgmspace.h>

extern const unsigned char font6x8[];

#endif
#endif
