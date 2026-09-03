#include "web_ui.h"

#if defined(ARDUINO_ARCH_ESP32) || defined(ARDUINO_ARCH_ESP8266)

#if defined(ARDUINO_ARCH_ESP32)
#include <WebServer.h>
#include <WiFi.h>
#else
// ESP8266's Arduino core ships its WiFi AP and HTTP server under different
// header/class names than ESP32's, but with an API compatible enough
// (softAP/softAPIP, on/begin/handleClient/hasArg/arg/send) that aliasing
// the class name lets the rest of this file stay board-agnostic.
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
using WebServer = ESP8266WebServer;
#endif

#include "calibration.h"
#include "rx5808.h"

namespace {
    // Access point the scanner hosts - connect to it directly, no existing
    // WiFi network required (handy in the pits/at the field).
    const char *AP_SSID = "BandoBuddy";
    const char *AP_PASSWORD = "raceband1";

    WebServer server(80);
    volatile uint8_t currentBusyMask = 0;

    // Raw RSSI per channel, updated once per sweep by setRssiRaw() and read
    // by the "/rssi" endpoint - the admin page's live calibration wizard
    // polls that to show/measure a live reading without needing its own
    // separate sampling path into the RX5808.
    uint16_t currentRssiRaw[RACEBAND_CHANNEL_COUNT] = {0};

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
  html, body { height: 100%; margin: 0; padding: 0; }
  body { font-family: sans-serif; background: #111; color: #eee; text-align: center; box-sizing: border-box; display: flex; flex-direction: column; overflow: hidden; }
  h1 { flex: 0 0 auto; font-size: 4vmin; margin: 1vmin 0; }
  h1 a { font-size: 0.6em; text-decoration: none; color: #9cf; vertical-align: middle; margin-left: 0.4em; }
  /* Fills whatever's left of the viewport below the title and centers the
     grid within it, so the grid reads at a glance from across the pit
     rather than sitting in a small fixed-size box. */
  .gridWrap { flex: 1 1 auto; min-height: 0; width: 100%; display: flex; align-items: center; justify-content: center; overflow: hidden; box-sizing: border-box; padding: 1vmin; }
  /* Sized to an exact 4:2 pixel box by JS (layoutGrid) - the largest that
     fits gridWrap - so the 25%/50% box dimensions below always divide out
     to a square, rather than however wide/tall the viewport happens to be.
     Always 4 columns x 2 rows: RACEBAND_CHANNEL_COUNT is a fixed 8 (see
     rx5808.h), same assumption tv_ui.cpp's GRID_COLS makes. */
  .grid { display: flex; flex-wrap: wrap; box-sizing: border-box; }
  /* Percentage width/height (not padding-driven) so every box is the same
     size whether it's showing one row (free/busy) or two (reserved with a
     pilot name). Font size is set on .grid by JS, scaled to match. */
  .box { box-sizing: border-box; width: calc(25% - 2vmin); height: calc(50% - 2vmin); margin: 1vmin; padding: 1vmin; border-radius: 8px; font-weight: bold; background: #444; cursor: pointer; display: flex; flex-direction: column; align-items: center; justify-content: center; overflow: hidden; }
  .free { background: #2ecc71; color: #063; }
  .busy { background: #e74c3c; color: #fff; }
  .reserved { background: #fff; color: #111; }
  .box .row { display: flex; align-items: center; justify-content: center; }
  .box .row > * + * { margin-left: 0.3em; }
  .box .row + .row { margin-top: 0.3em; }
  .box .icon { font-size: 0.7em; }
  .box small { font-size: 0.4em; font-weight: normal; }
  .box input { width: 80%; font-size: 0.5em; box-sizing: border-box; }
  .box button { margin-top: 0.3em; font-size: 0.5em; }
</style>
</head>
<body>
<h1>Raceband RSSI Scanner <a href="/admin" title="Admin settings">&#9881;</a></h1>
<div class="gridWrap" id="gridWrap"><div class="grid" id="grid"></div></div>
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
var gridWrap = document.getElementById('gridWrap');
// gridWrap fills whatever's left of the viewport below the title (CSS
// flex:1), but old WebKit (iOS 9 Safari) doesn't reliably resolve
// percentage heights on the boxes when an ancestor's own height comes from
// flex-grow rather than an explicit value. So instead of leaning on CSS
// percentages the whole way down, this measures gridWrap directly (which
// is accurate regardless of how its height was derived) and gives .grid an
// explicit 4:2 pixel size - the largest that still fits, keeping it
// centered - so the boxes divide out to squares instead of stretching to
// whatever the viewport's aspect ratio happens to be. Reruns on resize and
// on the address-bar show/hide that changes the viewport on mobile.
function layoutGrid() {
  var rect = gridWrap.getBoundingClientRect();
  var boxSize = Math.floor(Math.min(rect.width / 4, rect.height / 2));
  if (boxSize <= 0) return;
  grid.style.width = (boxSize * 4) + 'px';
  grid.style.height = (boxSize * 2) + 'px';
  grid.style.fontSize = Math.round(boxSize * 0.3) + 'px';
}
layoutGrid();
window.addEventListener('resize', layoutGrid);
window.addEventListener('orientationchange', layoutGrid);
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
    // Guards against a request hanging (e.g. a brief WiFi drop), which
    // would otherwise wedge the poll loop below since it waits on this
    // promise settling.
    xhr.timeout = 3000;
    xhr.onload = function () {
      if (xhr.status >= 200 && xhr.status < 300) {
        resolve(xhr.responseText);
      } else {
        reject(new Error('HTTP ' + xhr.status));
      }
    };
    xhr.onerror = function () { reject(new Error('network error')); };
    xhr.ontimeout = function () { reject(new Error('timeout')); };
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

// -- Keep the display awake (this runs pit-side; no one wants to tap the
// screen every 30s just to see who's on which channel). Prefer the native
// Wake Lock API where it exists; otherwise fall back to a hidden, looping,
// muted video, which WebKit treats as a reason not to sleep. NoSleep.js
// instead resets the idle timer on iOS < 10 via a same-page navigation
// that's cancelled with window.stop() before it loads, but on this app
// that trick tore down the page's own JS state (killing the live poll
// loop) - worse than the screen occasionally dimming - so it's not used
// here even though it's our actual target hardware (iPad Mini, iOS 9).
(function () {
  var enabled = false;
  var noSleepVideo = null;
  var NOSLEEP_MP4 = 'data:video/mp4;base64,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';

  function hasWakeLock() {
    return typeof navigator !== 'undefined' && 'wakeLock' in navigator;
  }

  function enableWakeLock() {
    if (enabled) return;
    if (hasWakeLock()) {
      navigator.wakeLock.request('screen').then(function () {
        enabled = true;
      }).catch(function () {});
    } else {
      if (!noSleepVideo) {
        noSleepVideo = document.createElement('video');
        noSleepVideo.setAttribute('title', 'No Sleep');
        noSleepVideo.setAttribute('playsinline', '');
        noSleepVideo.setAttribute('loop', '');
        noSleepVideo.muted = true;
        noSleepVideo.style.position = 'fixed';
        noSleepVideo.style.width = '1px';
        noSleepVideo.style.height = '1px';
        noSleepVideo.style.opacity = '0';
        noSleepVideo.src = NOSLEEP_MP4;
        document.body.appendChild(noSleepVideo);
      }
      var p = noSleepVideo.play();
      if (p && p.then) {
        p.then(function () { enabled = true; }).catch(function () {});
      } else {
        enabled = true;
      }
    }
  }

  document.addEventListener('click', enableWakeLock);
  document.addEventListener('touchstart', enableWakeLock);
})();

// Timer-driven (setInterval) rather than each cycle rescheduling the next
// one from its own promise's .then(): if a single cycle's request were to
// hang despite the httpGet timeout above, a self-rescheduling chain would
// never recover, whereas an independent interval tick keeps trying
// regardless. pollInFlight skips a tick while the previous one is still
// outstanding, so slow cycles don't pile up overlapping requests.
var pollInFlight = false;
function poll() {
  if (pollInFlight) return;
  pollInFlight = true;
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
      // Ignore - the next tick will retry.
    })
    .then(function () { pollInFlight = false; });
}
poll();
window.setInterval(poll, 500);
</script>
</body>
</html>
)HTML";

    // Raceband channel the "Calibrate" wizard watches live - R5 (5806MHz),
    // per its on-screen instructions to key up a VTX on that channel.
    constexpr uint8_t CALIBRATION_CHANNEL_INDEX = 4;

    const char ADMIN_PAGE_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>BandoBuddy Admin</title>
<style>
  body { font-family: sans-serif; background: #111; color: #eee; margin: 0; padding: 3vmin; box-sizing: border-box; }
  a.back { color: #9cf; text-decoration: none; font-size: 3.5vmin; }
  h1 { font-size: 5vmin; margin: 2vmin 0 4vmin; }
  .field { margin-bottom: 4vmin; }
  label { display: block; margin-bottom: 1.5vmin; font-size: 3.5vmin; }
  input[type=number] { font-size: 4vmin; width: 30vmin; max-width: 100%; padding: 1vmin; box-sizing: border-box; background: #222; color: #eee; border: 1px solid #555; border-radius: 4px; }
  .sliderRow { display: flex; align-items: center; }
  .sliderRow input[type=range] { flex: 1 1 auto; margin-right: 2vmin; }
  .sliderRow output { font-size: 4vmin; min-width: 10vmin; text-align: right; }
  button { font-size: 4vmin; padding: 1.5vmin 3vmin; margin: 1vmin 1vmin 1vmin 0; background: #333; color: #eee; border: 1px solid #555; border-radius: 4px; }
  button:disabled { opacity: 0.5; }
  #status { margin-top: 2vmin; font-size: 3.5vmin; min-height: 1.5em; }
  /* Fixed + explicit top/right/bottom/left (not the "inset" shorthand,
     which old WebKit - e.g. iOS 9 Safari on an iPad Mini - doesn't
     support) so this reliably covers the full screen above everything
     else, with its own scrollbar since a tall modal (the calibration
     wizard's step 2, with its live/peak readings and reset button) can
     exceed a small tablet's viewport height. Centering the modal via
     margin:auto on a scrollable block container, rather than the more common
     align-items:center flexbox trick, sidesteps a long-standing
     WebKit/Blink bug where a flex-centered child taller than its
     container can't be scrolled into full view - the parts above/below
     the fold stay permanently clipped even with overflow:auto. That bug
     is exactly what was truncating the modal's bottom on the iPad Mini. */
  #overlay { display: none; position: fixed; top: 0; right: 0; bottom: 0; left: 0; z-index: 1000; background: rgba(0,0,0,0.7); overflow-y: auto; padding: 3vmin; box-sizing: border-box; }
  #modal { background: #222; padding: 4vmin; border-radius: 8px; max-width: 100vmin; width: 100%; margin: 0 auto; box-sizing: border-box; }
  #modal h2 { margin-top: 0; font-size: 4.5vmin; }
  #modal p { font-size: 3.5vmin; }
  #modal .live { font-size: 7vmin; font-weight: bold; text-align: center; margin: 3vmin 0; }
  #modal .actions { display: flex; justify-content: flex-end; }
</style>
</head>
<body>
<a class="back" href="/">&larr; Back</a>
<h1>Admin</h1>

<div class="field">
  <label for="rssiMin">RSSI min (noise floor, raw ADC reading)</label>
  <input type="number" id="rssiMin" min="0" max="4095">
</div>
<div class="field">
  <label for="rssiMax">RSSI max (strong signal, raw ADC reading)</label>
  <input type="number" id="rssiMax" min="0" max="4095">
</div>
<div class="field">
  <label for="threshold">Busy threshold</label>
  <div class="sliderRow">
    <input type="range" id="threshold" min="0" max="100" step="1">
    <output id="thresholdOut"></output>
  </div>
</div>

<button id="saveBtn">Save</button>
<button id="calibrateBtn">Calibrate&hellip;</button>
<div id="status"></div>

<div id="overlay"><div id="modal"></div></div>

<script>
// ES5/XMLHttpRequest, same reasoning as the main status page's script - this
// can be opened from the same old goggle/tablet browsers.
function httpGet(url) {
  return new Promise(function (resolve, reject) {
    var xhr = new XMLHttpRequest();
    xhr.open('GET', url);
    xhr.timeout = 3000;
    xhr.onload = function () {
      if (xhr.status >= 200 && xhr.status < 300) {
        resolve(xhr.responseText);
      } else {
        reject(new Error('HTTP ' + xhr.status));
      }
    };
    xhr.onerror = function () { reject(new Error('network error')); };
    xhr.ontimeout = function () { reject(new Error('timeout')); };
    xhr.send();
  });
}

var rssiMinEl = document.getElementById('rssiMin');
var rssiMaxEl = document.getElementById('rssiMax');
var thresholdEl = document.getElementById('threshold');
var thresholdOut = document.getElementById('thresholdOut');
var statusEl = document.getElementById('status');

thresholdEl.addEventListener('input', function () {
  thresholdOut.textContent = thresholdEl.value + '%';
});

function loadValues() {
  httpGet('/calibration').then(function (text) {
    var data = JSON.parse(text);
    rssiMinEl.value = data.rssiMin;
    rssiMaxEl.value = data.rssiMax;
    thresholdEl.value = data.thresholdPct;
    thresholdOut.textContent = data.thresholdPct + '%';
  }).catch(function () {
    statusEl.textContent = 'Failed to load current settings.';
  });
}
loadValues();

function saveValues(min, max, thresholdPct, onDone) {
  httpGet('/calibration/set?min=' + min + '&max=' + max + '&thresholdPct=' + thresholdPct)
    .then(function () {
      statusEl.textContent = 'Saved.';
      onDone && onDone(true);
    })
    .catch(function () {
      statusEl.textContent = 'Failed to save - check RSSI min is less than max.';
      onDone && onDone(false);
    });
}

document.getElementById('saveBtn').addEventListener('click', function () {
  var min = parseInt(rssiMinEl.value, 10);
  var max = parseInt(rssiMaxEl.value, 10);
  var pct = parseInt(thresholdEl.value, 10);
  if (isNaN(min) || isNaN(max) || isNaN(pct) || min >= max) {
    statusEl.textContent = 'RSSI min must be a number less than RSSI max.';
    return;
  }
  saveValues(min, max, pct);
});

// -- "Calibrate" wizard: step 1 samples the noise floor with nothing
// transmitting, step 2 samples a VTX key-up on Raceband channel R5 (the
// live RSSI readings this polls are always for R5 specifically - see
// CALIBRATION_CHANNEL_INDEX server-side), then a review step lets the user
// save or discard the pair of readings before anything is written.
var overlay = document.getElementById('overlay');
var modal = document.getElementById('modal');
var wizardTimer = null;
var wizardPollInFlight = false;
var wizardMinCandidate = 0;
var wizardMaxCandidate = 0;

function stopWizardPolling() {
  if (wizardTimer) {
    window.clearInterval(wizardTimer);
    wizardTimer = null;
  }
}

function closeWizard() {
  stopWizardPolling();
  overlay.style.display = 'none';
}

function pollLiveRssi(onValue) {
  if (wizardPollInFlight) return;
  wizardPollInFlight = true;
  httpGet('/rssi')
    .then(function (text) {
      var raw = JSON.parse(text);
      onValue(raw[%CALIBRATION_CHANNEL_INDEX%]);
    })
    .catch(function () {
      // Ignore - the next tick will retry.
    })
    .then(function () { wizardPollInFlight = false; });
}

function showNoiseStep() {
  modal.innerHTML =
    '<h2>Calibrate - step 1 of 2</h2>' +
    '<p>Make sure no video transmitter is powered on near the receiver, then wait a moment for the reading below to settle.</p>' +
    '<div class="live" id="liveVal">&ndash;</div>' +
    '<div class="actions">' +
      '<button id="cancelBtn">Cancel</button>' +
      '<button id="nextBtn">Next</button>' +
    '</div>';
  document.getElementById('cancelBtn').addEventListener('click', closeWizard);

  var liveVal = document.getElementById('liveVal');
  var last = 0;
  stopWizardPolling();
  wizardTimer = window.setInterval(function () {
    pollLiveRssi(function (v) {
      last = v;
      liveVal.textContent = v;
    });
  }, 400);

  document.getElementById('nextBtn').addEventListener('click', function () {
    wizardMinCandidate = last;
    showSignalStep();
  });
}

function showSignalStep() {
  var peak = 0;
  modal.innerHTML =
    '<h2>Calibrate - step 2 of 2</h2>' +
    '<p>Power on a video transmitter on Raceband channel R5 (5806MHz) and place it about 2m (6ft) from the receiver antenna.</p>' +
    '<div class="live" id="liveVal">&ndash;</div>' +
    '<p>Peak so far: <span id="peakVal">&ndash;</span> <button id="resetPeakBtn">Reset</button></p>' +
    '<div class="actions">' +
      '<button id="cancelBtn">Cancel</button>' +
      '<button id="doneBtn">Done</button>' +
    '</div>';
  document.getElementById('cancelBtn').addEventListener('click', closeWizard);

  var liveVal = document.getElementById('liveVal');
  var peakVal = document.getElementById('peakVal');
  // In case a stray spike (or a first, badly-aimed VTX placement) throws
  // the peak off - start over without leaving/re-entering this step and
  // losing the live view.
  document.getElementById('resetPeakBtn').addEventListener('click', function () {
    peak = 0;
    peakVal.textContent = '–';
  });

  stopWizardPolling();
  wizardTimer = window.setInterval(function () {
    pollLiveRssi(function (v) {
      liveVal.textContent = v;
      if (v > peak) {
        peak = v;
        peakVal.textContent = v;
      }
    });
  }, 400);

  document.getElementById('doneBtn').addEventListener('click', function () {
    wizardMaxCandidate = peak;
    showReviewStep();
  });
}

function showReviewStep() {
  stopWizardPolling();
  var valid = wizardMaxCandidate > wizardMinCandidate;
  modal.innerHTML =
    '<h2>Review</h2>' +
    '<p>RSSI min: <b>' + wizardMinCandidate + '</b></p>' +
    '<p>RSSI max: <b>' + wizardMaxCandidate + '</b></p>' +
    (valid ? '' : '<p style="color:#e74c3c">Max must be greater than min - cancel and try again.</p>') +
    '<div class="actions">' +
      '<button id="cancelBtn">Cancel</button>' +
      '<button id="saveWizardBtn"' + (valid ? '' : ' disabled') + '>Save</button>' +
    '</div>';
  document.getElementById('cancelBtn').addEventListener('click', closeWizard);
  if (valid) {
    document.getElementById('saveWizardBtn').addEventListener('click', function () {
      var pct = parseInt(thresholdEl.value, 10) || 0;
      saveValues(wizardMinCandidate, wizardMaxCandidate, pct, function (ok) {
        if (ok) {
          rssiMinEl.value = wizardMinCandidate;
          rssiMaxEl.value = wizardMaxCandidate;
        }
        closeWizard();
      });
    });
  }
}

document.getElementById('calibrateBtn').addEventListener('click', function () {
  wizardMinCandidate = 0;
  wizardMaxCandidate = 0;
  overlay.style.display = 'block';
  showNoiseStep();
});
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

    void handleAdmin() {
        String page = FPSTR(ADMIN_PAGE_HTML);
        page.replace("%CALIBRATION_CHANNEL_INDEX%", String(CALIBRATION_CHANNEL_INDEX));
        server.send(200, "text/html", page);
    }

    void handleGetCalibration() {
        String json = "{\"rssiMin\":";
        json += Calibration::rssiMin();
        json += ",\"rssiMax\":";
        json += Calibration::rssiMax();
        json += ",\"thresholdPct\":";
        json += Calibration::busyThresholdPct();
        json += "}";
        server.send(200, "application/json", json);
    }

    // GET /calibration/set?min=<raw>&max=<raw>&thresholdPct=<0-100>. No
    // authentication, matching /reserve above and the rest of this
    // trust-based, pit-side device.
    void handleSetCalibration() {
        if (!server.hasArg("min") || !server.hasArg("max") || !server.hasArg("thresholdPct")) {
            server.send(400, "text/plain", "missing min/max/thresholdPct");
            return;
        }
        long rssiMin = server.arg("min").toInt();
        long rssiMax = server.arg("max").toInt();
        long thresholdPct = server.arg("thresholdPct").toInt();
        bool inRange = rssiMin >= 0 && rssiMin <= 0xFFFF && rssiMax >= 0 && rssiMax <= 0xFFFF &&
                       thresholdPct >= 0 && thresholdPct <= 100;
        if (!inRange || !Calibration::set(static_cast<uint16_t>(rssiMin), static_cast<uint16_t>(rssiMax),
                                           static_cast<uint8_t>(thresholdPct))) {
            server.send(400, "text/plain", "bad values (need min < max, threshold 0-100)");
            return;
        }
        server.send(200, "text/plain", "OK");
    }

    // Raw RSSI per channel, for the admin page's live calibration wizard.
    void handleRssi() {
        String json = "[";
        for (uint8_t i = 0; i < RACEBAND_CHANNEL_COUNT; i++) {
            if (i > 0) {
                json += ',';
            }
            json += currentRssiRaw[i];
        }
        json += ']';
        server.send(200, "application/json", json);
    }
}

namespace WebUi {
    void begin() {
        WiFi.softAP(AP_SSID, AP_PASSWORD);

        server.on("/", handleRoot);
        server.on("/state", handleState);
        server.on("/reservations", handleReservations);
        server.on("/reserve", handleReserve);
        server.on("/admin", handleAdmin);
        server.on("/calibration", handleGetCalibration);
        server.on("/calibration/set", handleSetCalibration);
        server.on("/rssi", handleRssi);
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

    void setRssiRaw(const uint16_t *rssiRaw) {
        for (uint8_t i = 0; i < RACEBAND_CHANNEL_COUNT; i++) {
            currentRssiRaw[i] = rssiRaw[i];
        }
    }

    void handleClient() {
        server.handleClient();
    }
}

#else

namespace WebUi {
    void begin() {}
    void setBusyMask(uint8_t) {}
    void setRssiRaw(const uint16_t *) {}
    void handleClient() {}
}

#endif
