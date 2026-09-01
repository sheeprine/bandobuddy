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

    // Pilot name reserving each channel, empty when the channel is free to
    // take. Held in RAM only - reservations don't survive a reboot.
    String pilotNames[RACEBAND_CHANNEL_COUNT];

    // Escapes a pilot name for embedding in a JSON string literal.
    String jsonEscape(const String &s) {
        String out;
        for (size_t i = 0; i < s.length(); i++) {
            char c = s[i];
            if (c == '"' || c == '\\') {
                out += '\\';
                out += c;
            } else if (c >= 0x20) {
                out += c;
            }
        }
        return out;
    }

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
  .box { padding: 24px 0; border-radius: 8px; font-size: 1.4em; font-weight: bold; background: #444; cursor: pointer; }
  .free { background: #2ecc71; color: #063; }
  .busy { background: #e74c3c; color: #fff; }
  .reserved { background: #fff; color: #111; }
  .box small { display: block; font-size: 0.6em; font-weight: normal; margin-top: 4px; }
  .box input { width: 80%; font-size: 0.7em; box-sizing: border-box; }
  .box button { margin-top: 6px; font-size: 0.7em; }
</style>
</head>
<body>
<h1>Raceband RSSI Scanner</h1>
<div class="grid" id="grid"></div>
<script>
const names = [%NAMES%];
const busy = new Array(names.length).fill(false);
const reserved = new Array(names.length).fill('');
let editingIndex = -1;
const grid = document.getElementById('grid');
names.forEach((name, i) => {
  const box = document.createElement('div');
  box.className = 'box free';
  box.id = 'ch-' + name;
  box.textContent = name;
  box.addEventListener('click', () => onBoxClick(i));
  grid.appendChild(box);
});

function render(i) {
  if (editingIndex === i) return;
  const box = document.getElementById('ch-' + names[i]);
  if (reserved[i]) {
    box.className = 'box ' + (busy[i] ? 'busy' : 'reserved');
    box.textContent = '';
    const label = document.createElement('div');
    label.textContent = names[i];
    const who = document.createElement('small');
    who.textContent = reserved[i];
    box.appendChild(label);
    box.appendChild(who);
  } else {
    box.className = 'box ' + (busy[i] ? 'busy' : 'free');
    box.textContent = names[i];
  }
}

function reserve(i, name) {
  fetch('/reserve?ch=' + i + '&name=' + encodeURIComponent(name))
    .then(() => { reserved[i] = name; render(i); })
    .catch(() => { render(i); });
}

function startEditing(i) {
  editingIndex = i;
  const box = document.getElementById('ch-' + names[i]);
  box.className = 'box free';
  box.textContent = '';

  const input = document.createElement('input');
  input.type = 'text';
  input.placeholder = 'Pilot name';
  input.maxLength = 16;
  input.addEventListener('click', (e) => e.stopPropagation());
  input.addEventListener('keydown', (e) => {
    if (e.key === 'Enter') submit();
  });

  const btn = document.createElement('button');
  btn.textContent = 'Reserve';
  btn.addEventListener('click', (e) => { e.stopPropagation(); submit(); });

  function submit() {
    const val = input.value.trim();
    editingIndex = -1;
    if (val) {
      reserve(i, val);
    } else {
      render(i);
    }
  }

  box.appendChild(input);
  box.appendChild(btn);
  input.focus();
}

function onBoxClick(i) {
  if (reserved[i]) {
    reserve(i, '');
    return;
  }
  if (editingIndex !== -1) return;
  startEditing(i);
}

async function poll() {
  try {
    const mask = parseInt(await (await fetch('/state')).text(), 10);
    const list = await (await fetch('/reservations')).json();
    names.forEach((name, i) => {
      busy[i] = (mask & (1 << i)) !== 0;
      reserved[i] = list[i] || '';
      render(i);
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

    void handleReservations() {
        String json = "[";
        for (uint8_t i = 0; i < RACEBAND_CHANNEL_COUNT; i++) {
            if (i > 0) {
                json += ',';
            }
            json += '"';
            json += jsonEscape(pilotNames[i]);
            json += '"';
        }
        json += ']';
        server.send(200, "application/json", json);
    }

    // GET /reserve?ch=<index>&name=<pilot name>. An empty name clears the
    // reservation. No authentication - any client on the AP can set or
    // clear any channel's reservation, matching the rest of this
    // trust-based, pit-side device.
    void handleReserve() {
        if (!server.hasArg("ch")) {
            server.send(400, "text/plain", "missing ch");
            return;
        }
        int ch = server.arg("ch").toInt();
        if (ch < 0 || ch >= RACEBAND_CHANNEL_COUNT) {
            server.send(400, "text/plain", "bad ch");
            return;
        }
        pilotNames[ch] = server.arg("name");
        server.send(200, "text/plain", "OK");
    }
}

namespace WebUi {
    void begin() {
        WiFi.softAP(AP_SSID, AP_PASSWORD);

        server.on("/", handleRoot);
        server.on("/state", handleState);
        server.on("/reservations", handleReservations);
        server.on("/reserve", handleReserve);
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
