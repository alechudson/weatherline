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
var PLATFORM_CODES = {1: 'emery', 2: 'gabbro'};

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

function queueSend(item) {
  pending = item;
  attempts = 0;
  if (retryTimer) {
    clearTimeout(retryTimer);
    retryTimer = null;
  }
  if (!inflight) sendPending();
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
    if (attempts > 5) {
      pending = null;
      attempts = 0;
      return;
    }
    retryTimer = setTimeout(function() {
      retryTimer = null;
      sendPending();
    }, 2000);
  });
}

function sendForecast(cached) {
  if (!cached) return;
  var st = settings.readSettings(localStorage);
  var bytes = frame.buildFrame(cached, st, watchPlatform(), nowUnix());
  if (!bytes) return;
  var unpacked = frame.unpackFrame(bytes);
  queueSend({
    bytes: bytes,
    lat: cached.lat,
    lon: cached.lon,
    hours: unpacked ? unpacked.hourCount : 0
  });
}

function skipFetch(cached, fix, st) {
  if (!cached || !fix || !isNum(cached.fetchedAt)) return false;
  var ageSec = nowUnix() - cached.fetchedAt;
  if (!(ageSec < st.refreshMin * 60)) return false;
  return locate.samePlace(cached, fix);
}

function armRefresh() {
  if (refreshTimer) clearTimeout(refreshTimer);
  var st = settings.readSettings(localStorage);
  refreshTimer = setTimeout(function() {
    refreshTimer = null;
    cycle();
  }, st.refreshMin * 60000);
}

function finish(gen) {
  if (gen !== generation) return;
  busySince = 0;
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
    var place = '';
    if (previous && locate.samePlace(previous, fix)) {
      lat = previous.lat;
      lon = previous.lon;
      place = previous.place || '';
    }
    var stored = {
      fetchedAt: parsed.fetchedAt,
      utcOffsetSeconds: parsed.utcOffsetSeconds,
      currentC: parsed.currentC,
      hours: parsed.hours,
      days: parsed.days,
      lat: lat,
      lon: lon,
      place: place
    };
    forecast.writeCache(localStorage, stored);
    sendForecast(stored);
    requestPlace(lat, lon, gen);
    finish(gen);
  };
  xhr.onerror = function() {
    if (gen !== generation) return;
    sendForecast(forecast.readCache(localStorage));
    finish(gen);
  };
  xhr.ontimeout = xhr.onerror;
  xhr.send();
}

function cycle() {
  var gen = ++generation;
  busySince = Date.now();
  var st = settings.readSettings(localStorage);
  var cached = forecast.readCache(localStorage);
  sendForecast(cached);
  locate.resolveFix({
    storage: localStorage,
    nowMs: Date.now(),
    geolocate: geolocate,
    stillCurrent: function() { return gen === generation; }
  }).then(function(fix) {
    if (gen !== generation) return;
    if (!fix || skipFetch(cached, fix, st)) {
      finish(gen);
      return;
    }
    fetchForecast(fix, gen);
  }, function() {
    finish(gen);
  });
}

// A refresh already under way answers the request; one stuck for two minutes is replaced.
function busy() {
  return busySince > 0 && Date.now() - busySince < 120000;
}

Pebble.addEventListener('ready', function() {
  cycle();
});

Pebble.addEventListener('appmessage', function(e) {
  var payload = (e && e.payload) || {};
  var code = payload.REQUEST;
  if (code === undefined) code = payload[messageKeys.REQUEST];
  if (code === undefined) return;
  if (PLATFORM_CODES[code]) localStorage.setItem(PLATFORM_KEY, PLATFORM_CODES[code]);
  if (!busy()) cycle();
});

Pebble.addEventListener('showConfiguration', function() {
  Pebble.openURL(settings.configPage(settings.readSettings(localStorage)));
});

Pebble.addEventListener('webviewclosed', function(e) {
  var stored = settings.applyConfig(localStorage, e && e.response);
  if (!stored) return;
  // Redraw the stored forecast with the new settings. A refresh in flight keeps going
  // and re-arms the timer with the new interval when it ends.
  sendForecast(forecast.readCache(localStorage));
  if (!busy()) armRefresh();
});
