var forecast = require('./forecast');
var frame = require('./frame');
var locate = require('./locate');
var settings = require('./settings');
var messageKeys = require('message_keys');

var generation = 0;
var pending = null;
var inflight = false;
var attempts = 0;
var retryTimer = null;
var refreshTimer = null;
var busySince = 0;
var PLATFORM_KEY = 'weatherline.platform';
var CHECKED_KEY = 'weatherline.checkedAt';
var SENT_HASH_KEY = 'weatherline.lastSentHash';
var PLATFORM_CODES = {1: 'emery', 2: 'gabbro'};
var REQUEST_NEED_FRAME = 0x80;
var MAX_SEND_RETRIES = 3;

function readCheckedAt() {
  var raw = localStorage.getItem(CHECKED_KEY);
  if (!raw) return 0;
  var n = Number(raw);
  return typeof n === 'number' && isFinite(n) && n > 0 ? n : 0;
}

// What the watch last acknowledged, persisted by hash across JS restarts so switching
// away from the watchface and back does not re-send an unchanged frame over Bluetooth.
var lastSent = null;
var lastSentHash = localStorage.getItem(SENT_HASH_KEY) || null;
// When a refresh last checked the location and had a forecast to show, in ms.
var checkedAt = readCheckedAt();

function isNum(value) {
  return typeof value === 'number' && isFinite(value);
}

function nowUnix() {
  return Math.floor(Date.now() / 1000);
}

function watchPlatform() {
  var info = null;
  try {
    info = Pebble.getActiveWatchInfo();
  } catch (e) {
    info = null;
  }
  if (info) {
    var platform = info.platform ? String(info.platform).toLowerCase() : '';
    if (platform === 'emery' || platform === 'gabbro') return platform;
    var model = info.model ? String(info.model).toLowerCase() : '';
    if (model.indexOf('gabbro') >= 0) return 'gabbro';
    if (model.indexOf('emery') >= 0) return 'emery';
  }
  // The watch names its own platform when it asks for a refresh.
  var told = localStorage.getItem(PLATFORM_KEY);
  return told === 'emery' || told === 'gabbro' ? told : null;
}

function geolocate(highAccuracy) {
  var options = locate.geoOptions(highAccuracy);
  return new Promise(function(resolve, reject) {
    if (!navigator.geolocation || !navigator.geolocation.getCurrentPosition) {
      reject(new Error('geolocation unavailable'));
      return;
    }
    navigator.geolocation.getCurrentPosition(resolve, reject, options);
  });
}

function frameHash(bytes) {
  var h = 2166136261;
  var i;
  for (i = 0; i < bytes.length; i++) {
    h ^= bytes[i];
    h = (h + (h << 1) + (h << 4) + (h << 7) + (h << 8) + (h << 24)) >>> 0;
  }
  return bytes.length + ':' + h.toString(16);
}

function sameBytes(a, b) {
  if (!a || !b || a.length !== b.length) return false;
  var i;
  for (i = 0; i < a.length; i++) {
    if (a[i] !== b[i]) return false;
  }
  return true;
}

function queueSend(item) {
  item.hash = frameHash(item.bytes);
  if (pending) {
    if (sameBytes(item.bytes, pending.bytes)) return false;
  } else if (lastSent) {
    if (sameBytes(item.bytes, lastSent)) return false;
  } else if (lastSentHash && item.hash === lastSentHash) {
    return false;
  }
  pending = item;
  attempts = 0;
  if (retryTimer) {
    clearTimeout(retryTimer);
    retryTimer = null;
  }
  if (!inflight) sendPending();
  return true;
}

function sendPending() {
  if (!pending) return;
  var item = pending;
  var bytes = [];
  var i;
  for (i = 0; i < item.bytes.length; i++) bytes.push(item.bytes[i]);
  var dict = {};
  dict[messageKeys.PAYLOAD] = bytes;
  inflight = true;
  Pebble.sendAppMessage(dict, function() {
    inflight = false;
    console.log('weatherline sent ' + item.lat + ' ' + item.lon + ' hours ' + item.hours);
    lastSent = item.bytes;
    lastSentHash = item.hash;
    localStorage.setItem(SENT_HASH_KEY, lastSentHash);
    if (pending === item) pending = null;
    attempts = 0;
    if (pending) sendPending();
  }, function() {
    inflight = false;
    if (pending !== item) {
      attempts = 0;
      sendPending();
      return;
    }
    attempts += 1;
    if (attempts > MAX_SEND_RETRIES) {
      pending = null;
      attempts = 0;
      return;
    }
    var delay = 2000 * (1 << (attempts - 1));
    retryTimer = setTimeout(function() {
      retryTimer = null;
      sendPending();
    }, delay);
  });
}

function sendForecast(cached) {
  if (!cached) return false;
  var st = settings.readSettings(localStorage);
  var bytes = frame.buildFrame(cached, st, watchPlatform(), nowUnix());
  if (!bytes) return false;
  return queueSend({
    bytes: bytes,
    lat: cached.lat,
    lon: cached.lon,
    hours: bytes[3]
  });
}

// The top-row and alert preferences have to reach the watch even before any forecast arrives.
function sendTopLine() {
  var st = settings.readSettings(localStorage);
  var dict = {};
  dict[messageKeys.TOPLINE] = frame.packPrefs(st);
  Pebble.sendAppMessage(dict, function() {}, function() {});
}

function skipFetch(cached, fix, st) {
  if (!cached || !fix || !isNum(cached.fetchedAt)) return false;
  var ageSec = nowUnix() - cached.fetchedAt;
  if (!(ageSec < st.refreshMin * 60)) return false;
  return locate.samePlace(cached, fix);
}

function armRefresh(delayMs) {
  if (refreshTimer) clearTimeout(refreshTimer);
  var st = settings.readSettings(localStorage);
  var wait = isNum(delayMs) && delayMs >= 1000 ? delayMs : st.refreshMin * 60000;
  refreshTimer = setTimeout(function() {
    refreshTimer = null;
    cycle();
  }, wait);
}

// `checked` is true when this refresh found the location and has a current forecast; only
// then may a watch request inside the refresh interval be answered without a new one.
function finish(gen, checked) {
  if (gen !== generation) return;
  busySince = 0;
  if (checked) {
    checkedAt = Date.now();
    localStorage.setItem(CHECKED_KEY, String(checkedAt));
  }
  armRefresh();
}

function requestPlace(lat, lon, gen) {
  var xhr = new XMLHttpRequest();
  xhr.open('GET', forecast.placeUrl(lat, lon));
  xhr.timeout = 4000;
  xhr.onload = function() {
    if (gen !== generation || xhr.status !== 200) return;
    var name = forecast.placeFromBody(xhr.responseText);
    if (!name) return;
    var current = forecast.readCache(localStorage);
    if (!current || !locate.samePlace(current, {lat: lat, lon: lon})) return;
    current.place = name;
    forecast.writeCache(localStorage, current);
    sendForecast(current);
  };
  xhr.onerror = function() {};
  xhr.ontimeout = function() {};
  xhr.send();
}

function fetchForecast(fix, gen) {
  var xhr = new XMLHttpRequest();
  xhr.open('GET', forecast.forecastUrl(fix.lat, fix.lon));
  xhr.timeout = 20000;
  xhr.onload = function() {
    if (gen !== generation) return;
    if (xhr.status !== 200) {
      sendForecast(forecast.readCache(localStorage));
      finish(gen);
      return;
    }
    var parsed = forecast.parseForecast(xhr.responseText, nowUnix());
    if (!parsed) {
      sendForecast(forecast.readCache(localStorage));
      finish(gen);
      return;
    }
    var previous = forecast.readCache(localStorage);
    var lat = fix.lat;
    var lon = fix.lon;
    var place = fix.place ? forecast.clipPlace(fix.place) : '';
    if (!place && previous && locate.samePlace(previous, fix)) {
      lat = previous.lat;
      lon = previous.lon;
      place = previous.place || '';
    }
    var stored = {
      fetchedAt: parsed.fetchedAt,
      utcOffsetSeconds: parsed.utcOffsetSeconds,
      currentC: parsed.currentC,
      currentApparentC: parsed.currentApparentC,
      hours: parsed.hours,
      days: parsed.days,
      lat: lat,
      lon: lon,
      place: place
    };
    forecast.writeCache(localStorage, stored);
    sendForecast(stored);
    // The name only changes with the place, so a known one is kept.
    if (!place) requestPlace(lat, lon, gen);
    finish(gen, true);
  };
  xhr.onerror = function() {
    if (gen !== generation) return;
    sendForecast(forecast.readCache(localStorage));
    finish(gen);
  };
  xhr.ontimeout = xhr.onerror;
  xhr.send();
}

function geocodeFixed(query) {
  var coords = forecast.parseCoords(query);
  if (coords) {
    return Promise.resolve({lat: coords.lat, lon: coords.lon, place: ''});
  }
  return new Promise(function(resolve) {
    var xhr = new XMLHttpRequest();
    xhr.open('GET', forecast.geocodeUrl(query));
    xhr.timeout = 8000;
    xhr.onload = function() {
      if (xhr.status !== 200) {
        resolve(null);
        return;
      }
      resolve(forecast.geocodeFromBody(xhr.responseText));
    };
    xhr.onerror = function() { resolve(null); };
    xhr.ontimeout = xhr.onerror;
    xhr.send();
  });
}

function resolveTarget(st, gen) {
  if (st.locationMode === 'fixed') {
    if (st.fixedQuery) {
      return geocodeFixed(st.fixedQuery).then(function(found) {
        if (gen !== generation) return null;
        if (found) {
          var updated = settings.readSettings(localStorage);
          updated.fixedLat = found.lat;
          updated.fixedLon = found.lon;
          updated.fixedPlace = found.place || updated.fixedPlace || '';
          updated.fixedQuery = '';
          settings.writeSettings(localStorage, updated);
          return found;
        }
        if (isNum(st.fixedLat) && isNum(st.fixedLon)) {
          return {lat: st.fixedLat, lon: st.fixedLon, place: st.fixedPlace || ''};
        }
        return null;
      });
    }
    if (isNum(st.fixedLat) && isNum(st.fixedLon)) {
      return Promise.resolve({lat: st.fixedLat, lon: st.fixedLon, place: st.fixedPlace || ''});
    }
  }
  return locate.resolveFix({
    storage: localStorage,
    nowMs: Date.now(),
    geolocate: geolocate,
    stillCurrent: function() { return gen === generation; }
  });
}

// The stored forecast goes to the watch first only when the face has just started; on a
// later refresh the watch already shows it, and the result follows soon.
function cycle(sendFirst) {
  var gen = ++generation;
  busySince = Date.now();
  var st = settings.readSettings(localStorage);
  var cached = forecast.readCache(localStorage);
  if (sendFirst) sendForecast(cached);
  resolveTarget(st, gen).then(function(fix) {
    if (gen !== generation) return;
    if (!fix) {
      sendForecast(cached);
      finish(gen);
      return;
    }
    if (skipFetch(cached, fix, st)) {
      // The frame can still change without a fetch, at midnight say.
      sendForecast(cached);
      finish(gen, true);
      return;
    }
    fetchForecast(fix, gen);
  }, function() {
    if (gen !== generation) return;
    sendForecast(cached);
    finish(gen);
  });
}

// A refresh already under way answers the request; one stuck for two minutes is replaced.
function busy() {
  return busySince > 0 && Date.now() - busySince < 120000;
}

Pebble.addEventListener('ready', function() {
  var cached = forecast.readCache(localStorage);
  if (cached && recentlyChecked()) {
    // Re-launching the watchface inside the refresh window skips geolocation and network;
    // the cached frame only goes over Bluetooth if it changed (at midnight, say).
    sendForecast(cached);
    var st = settings.readSettings(localStorage);
    armRefresh(st.refreshMin * 60000 - (Date.now() - checkedAt));
    return;
  }
  cycle(true);
});

// A refresh inside the interval already found the place and a fresh forecast.
function recentlyChecked() {
  if (!checkedAt) return false;
  var st = settings.readSettings(localStorage);
  var elapsed = Date.now() - checkedAt;
  return elapsed >= 0 && elapsed < st.refreshMin * 60000;
}

Pebble.addEventListener('appmessage', function(e) {
  var payload = (e && e.payload) || {};
  var raw = payload.REQUEST;
  if (raw === undefined) raw = payload[messageKeys.REQUEST];
  if (raw === undefined) return;
  var needFrame = (raw & REQUEST_NEED_FRAME) !== 0;
  var code = raw & 0x0F;
  if (PLATFORM_CODES[code]) localStorage.setItem(PLATFORM_KEY, PLATFORM_CODES[code]);
  if (needFrame) {
    lastSent = null;
    lastSentHash = null;
    localStorage.removeItem(SENT_HASH_KEY);
  }
  if (busy()) {
    if (needFrame) sendForecast(forecast.readCache(localStorage));
    return;
  }
  if (recentlyChecked()) {
    // Nothing new to fetch; the watch gets the stored frame only if it has changed.
    sendForecast(forecast.readCache(localStorage));
    return;
  }
  cycle(needFrame);
});

Pebble.addEventListener('showConfiguration', function() {
  Pebble.openURL(settings.configPage(settings.readSettings(localStorage)));
});

Pebble.addEventListener('webviewclosed', function(e) {
  var prev = settings.readSettings(localStorage);
  var stored = settings.applyConfig(localStorage, e && e.response);
  if (!stored) return;
  var locationChanged = prev.locationMode !== stored.locationMode
    || prev.fixedLat !== stored.fixedLat
    || prev.fixedLon !== stored.fixedLon
    || (stored.locationMode === 'fixed' && !!stored.fixedQuery);
  if (locationChanged) {
    checkedAt = 0;
    localStorage.removeItem(CHECKED_KEY);
    cycle(false);
    return;
  }
  // Redraw the stored forecast with the new settings. A refresh in flight keeps going
  // and re-arms the timer with the new interval when it ends. With no forecast yet, the
  // top-row and alert preferences still go to the watch on their own.
  var cached = forecast.readCache(localStorage);
  if (!sendForecast(cached) && !cached && !busy()) sendTopLine();
  if (!busy()) armRefresh();
});
