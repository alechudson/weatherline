var FIX_KEY = 'weatherline.fix';
// If network location fails indoors, reuse a cached fix up to this old before waking GPS.
var GPS_CACHE_MAX_MS = 6 * 3600 * 1000;

function isNum(value) {
  return typeof value === 'number' && isFinite(value);
}

function geoOptions(highAccuracy) {
  if (highAccuracy) {
    return {enableHighAccuracy: true, timeout: 15000, maximumAge: 0};
  }
  return {enableHighAccuracy: false, timeout: 8000, maximumAge: 900000};
}

function samePlace(a, b) {
  if (!a || !b || !isNum(a.lat) || !isNum(a.lon) || !isNum(b.lat) || !isNum(b.lon)) {
    return false;
  }
  return Math.abs(a.lat - b.lat) < 0.02 && Math.abs(a.lon - b.lon) < 0.02;
}

function readFix(storage) {
  var text = storage.getItem(FIX_KEY);
  if (!text) return null;
  var value;
  try {
    value = JSON.parse(text);
  } catch (e) {
    return null;
  }
  if (!value || !isNum(value.lat) || !isNum(value.lon)) return null;
  return {lat: value.lat, lon: value.lon, at: value.at};
}

function alive(opts) {
  if (!opts || typeof opts.stillCurrent !== 'function') return true;
  return opts.stillCurrent() !== false;
}

// Timestamps below 1e12 are seconds, so they have no millisecond age to reject.
function readingFresh(pos, maximumAge, nowMs) {
  if (!pos || !pos.coords || !isNum(pos.coords.latitude) || !isNum(pos.coords.longitude)) {
    return false;
  }
  var ts = pos.timestamp;
  if (ts === undefined || ts === null || ts < 1e12) return true;
  if (!isNum(ts)) return false;
  return (nowMs - ts) <= (maximumAge + 60000);
}

function storeFix(storage, pos, nowMs) {
  var lat = pos.coords.latitude;
  var lon = pos.coords.longitude;
  var prev = readFix(storage);
  if (prev && samePlace(prev, {lat: lat, lon: lon})) {
    lat = prev.lat;
    lon = prev.lon;
  }
  storage.setItem(FIX_KEY, JSON.stringify({lat: lat, lon: lon, at: nowMs}));
  return {lat: lat, lon: lon};
}

function callGeo(geolocate, highAccuracy) {
  return new Promise(function(resolve, reject) {
    var result;
    try {
      result = geolocate(highAccuracy);
    } catch (e) {
      reject(e);
      return;
    }
    if (result && typeof result.then === 'function') {
      result.then(resolve, reject);
      return;
    }
    resolve(result);
  });
}

function tryFix(opts, highAccuracy) {
  if (!alive(opts)) return Promise.resolve(null);
  var maximumAge = geoOptions(highAccuracy).maximumAge;
  return callGeo(opts.geolocate, highAccuracy).then(function(pos) {
    if (!alive(opts) || !readingFresh(pos, maximumAge, opts.nowMs)) return null;
    return storeFix(opts.storage, pos, opts.nowMs);
  }, function() {
    return null;
  });
}

function cachedFreshForGpsSkip(cached, nowMs) {
  if (!cached || !isNum(cached.at) || !isNum(nowMs)) return false;
  var age = nowMs - cached.at;
  return age >= 0 && age <= GPS_CACHE_MAX_MS;
}

function resolveFix(opts) {
  return tryFix(opts, false).then(function(fix) {
    if (fix) return fix;
    if (!alive(opts)) return null;
    var cached = readFix(opts.storage);
    if (cachedFreshForGpsSkip(cached, opts.nowMs)) {
      return {lat: cached.lat, lon: cached.lon};
    }
    return tryFix(opts, true).then(function(gpsFix) {
      if (gpsFix) return gpsFix;
      if (!alive(opts) || !cached) return null;
      return {lat: cached.lat, lon: cached.lon};
    });
  });
}

module.exports = {
  geoOptions: geoOptions,
  samePlace: samePlace,
  resolveFix: resolveFix
};
