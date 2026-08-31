#include "web_ui.h"

#if defined(ARDUINO_ARCH_ESP32)

#include <WebServer.h>
#include <WiFi.h>

#include "rx5808.h"

namespace {
    // Access point the scanner hosts - connect to it directly, no existing
    // WiFi network required (handy in the pits/at the field).
    const char *AP_SSID = "BandoBuddy";
    const char *AP_PASSWORD = "raceband1";

    WebServer server(80);
    volatile uint8_t currentBusyMask = 0;

    // %NAMES% is filled in with the channel name list at request time.
    const char PAGE_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>BandoBuddy</title>
<style>
  body { font-family: sans-serif; background: #111; color: #eee; text-align: center; }
  h1 { font-size: 1.2em; }
  .grid { display: grid; grid-template-columns: repeat(4, 1fr); gap: 10px; max-width: 480px; margin: 20px auto; }
  .box { padding: 24px 0; border-radius: 8px; font-size: 1.4em; font-weight: bold; background: #444; }
  .free { background: #2ecc71; color: #063; }
  .busy { background: #e74c3c; color: #fff; }
</style>
</head>
<body>
<h1>Raceband RSSI Scanner</h1>
<div class="grid" id="grid"></div>
<script>
const names = [%NAMES%];
const grid = document.getElementById('grid');
names.forEach((name) => {
  const box = document.createElement('div');
  box.className = 'box free';
  box.id = 'ch-' + name;
  box.textContent = name;
  grid.appendChild(box);
});

async function poll() {
  try {
    const mask = parseInt(await (await fetch('/state')).text(), 10);
    names.forEach((name, i) => {
      const busy = (mask & (1 << i)) !== 0;
      document.getElementById('ch-' + name).className = 'box ' + (busy ? 'busy' : 'free');
    });
  } catch (e) {
    // Ignore - the next poll will retry.
  }
  setTimeout(poll, 500);
}
poll();
</script>
</body>
</html>
)HTML";

    void handleRoot() {
        String namesJs;
        for (uint8_t i = 0; i < RACEBAND_CHANNEL_COUNT; i++) {
            if (i > 0) {
                namesJs += ',';
            }
            namesJs += '\'';
            namesJs += RACEBAND_CHANNEL_NAMES[i];
            namesJs += '\'';
        }

        String page = FPSTR(PAGE_HTML);
        page.replace("%NAMES%", namesJs);
        server.send(200, "text/html", page);
    }

    void handleState() {
        server.send(200, "text/plain", String(currentBusyMask));
    }
}

namespace WebUi {
    void begin() {
        WiFi.softAP(AP_SSID, AP_PASSWORD);

        server.on("/", handleRoot);
        server.on("/state", handleState);
        server.begin();

        Serial.print(F("Web UI: join WiFi \""));
        Serial.print(AP_SSID);
        Serial.print(F("\" (password \""));
        Serial.print(AP_PASSWORD);
        Serial.print(F("\") and browse to http://"));
        Serial.println(WiFi.softAPIP());
    }

    void setBusyMask(uint8_t busyMask) {
        currentBusyMask = busyMask;
    }

    void handleClient() {
        server.handleClient();
    }
}

#else

namespace WebUi {
    void begin() {}
    void setBusyMask(uint8_t) {}
    void handleClient() {}
}

#endif
