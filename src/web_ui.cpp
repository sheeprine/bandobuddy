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
  .grid { display: flex; flex-wrap: wrap; max-width: 480px; margin: 20px auto; }
  .box { box-sizing: border-box; width: calc(25% - 10px); margin: 5px; padding: 24px 0; border-radius: 8px; font-size: 1.4em; font-weight: bold; background: #444; cursor: pointer; }
  .free { background: #2ecc71; color: #063; }
  .busy { background: #e74c3c; color: #fff; }
  .reserved { background: #fff; color: #111; }
  .box .row { display: flex; align-items: center; justify-content: center; }
  .box .row > * + * { margin-left: 6px; }
  .box .row + .row { margin-top: 6px; }
  .box .icon { font-size: 0.7em; }
  .box small { font-size: 0.6em; font-weight: normal; }
  .box input { width: 80%; font-size: 0.7em; box-sizing: border-box; }
  .box button { margin-top: 6px; font-size: 0.7em; }
</style>
</head>
<body>
<h1>Raceband RSSI Scanner</h1>
<div class="grid" id="grid"></div>
<script>
// Written in ES5 and using XMLHttpRequest rather than fetch/async-await:
// this page runs on old FPV-goggle/tablet browsers (e.g. iOS 9 Safari on an
// original iPad Mini) where those newer features are unavailable. A single
// unsupported syntax construct anywhere in this script would abort parsing
// of the whole block, leaving the channel boxes unrendered.
var names = [%NAMES%];
var busy = [];
var reserved = [];
for (var n = 0; n < names.length; n++) {
  busy.push(false);
  reserved.push('');
}
var editingIndex = -1;
var grid = document.getElementById('grid');
// Distinct symbol per busy state, not just color, so colorblind pilots can
// tell channels apart without relying on red/green.
var STATE_ICONS = { free: '✓', busy: '⚠︎' };
names.forEach(function (name, i) {
  var box = document.createElement('div');
  box.id = 'ch-' + name;
  box.addEventListener('click', function () { onBoxClick(i); });
  grid.appendChild(box);
  render(i);
});

function render(i) {
  if (editingIndex === i) return;
  var box = document.getElementById('ch-' + names[i]);
  var state = busy[i] ? 'busy' : 'free';
  box.className = 'box ' + (reserved[i] ? (busy[i] ? 'busy' : 'reserved') : state);
  box.textContent = '';

  var nameRow = document.createElement('div');
  nameRow.className = 'row';
  var label = document.createElement('span');
  label.textContent = names[i];
  var icon = document.createElement('span');
  icon.className = 'icon';
  icon.textContent = STATE_ICONS[state];
  nameRow.appendChild(label);
  nameRow.appendChild(icon);
  box.appendChild(nameRow);

  if (reserved[i]) {
    var pilotRow = document.createElement('div');
    pilotRow.className = 'row';
    var lock = document.createElement('span');
    lock.className = 'icon';
    lock.textContent = '🔒';
    var who = document.createElement('small');
    who.textContent = reserved[i];
    pilotRow.appendChild(lock);
    pilotRow.appendChild(who);
    box.appendChild(pilotRow);
  }
}

function httpGet(url) {
  return new Promise(function (resolve, reject) {
    var xhr = new XMLHttpRequest();
    xhr.open('GET', url);
    xhr.onload = function () {
      if (xhr.status >= 200 && xhr.status < 300) {
        resolve(xhr.responseText);
      } else {
        reject(new Error('HTTP ' + xhr.status));
      }
    };
    xhr.onerror = function () { reject(new Error('network error')); };
    xhr.send();
  });
}

function reserve(i, name) {
  httpGet('/reserve?ch=' + i + '&name=' + encodeURIComponent(name))
    .then(function () { reserved[i] = name; render(i); })
    .catch(function () { render(i); });
}

function startEditing(i) {
  editingIndex = i;
  var box = document.getElementById('ch-' + names[i]);
  box.className = 'box free';
  box.textContent = '';

  var input = document.createElement('input');
  input.type = 'text';
  input.placeholder = 'Pilot name';
  input.maxLength = 16;
  input.addEventListener('click', function (e) { e.stopPropagation(); });
  input.addEventListener('keydown', function (e) {
    if (e.key === 'Enter' || e.keyCode === 13) submit();
  });

  var btn = document.createElement('button');
  btn.textContent = 'Reserve';
  btn.addEventListener('click', function (e) { e.stopPropagation(); submit(); });

  function submit() {
    var val = input.value.trim();
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

function poll() {
  Promise.all([httpGet('/state'), httpGet('/reservations')])
    .then(function (results) {
      var mask = parseInt(results[0], 10);
      var list = JSON.parse(results[1]);
      names.forEach(function (name, i) {
        busy[i] = (mask & (1 << i)) !== 0;
        reserved[i] = list[i] || '';
        render(i);
      });
    })
    .catch(function () {
      // Ignore - the next poll will retry.
    })
    .then(function () { setTimeout(poll, 500); });
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
