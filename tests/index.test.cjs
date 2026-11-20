var test = require('node:test');
var assert = require('node:assert/strict');
var fs = require('fs');
var path = require('path');
var Module = require('module');

var NOW_MS = 1794790800 * 1000;

function memory() {
  var data = {};
  return {
    getItem: function(k) { return Object.prototype.hasOwnProperty.call(data, k) ? data[k] : null; },
    setItem: function(k, v) { data[k] = String(v); },
    removeItem: function(k) { delete data[k]; }
  };
}

// Loads src/pkjs/index.js against a fake Pebble runtime and returns the hooks a test drives.
function boot(options) {
  var listeners = {};
  var sent = [];
  var requests = [];
  var timers = [];
  var geoCalls = 0;
  var storage = options.storage || memory();
  if (!options.storage) {
    var cachedFix = JSON.stringify({lat: 30, lon: -97, at: NOW_MS});
    storage.setItem('weatherline.fix', cachedFix);
  }

  function FakeXhr() {
    requests.push(this);
  }
  FakeXhr.prototype.open = function(method, url) { this.url = url; };
  FakeXhr.prototype.send = function() {};

  var globals = {
    Pebble: {
      addEventListener: function(name, fn) { listeners[name] = fn; },
      getActiveWatchInfo: function() { return options.watchInfo || null; },
      sendAppMessage: function(dict, ok, fail) {
        sent.push(dict);
        if (options.failSend) fail();
        else ok();
      },
      openURL: function() {}
    },
    localStorage: storage,
    navigator: {
      geolocation: {
        getCurrentPosition: function(ok, fail) {
          geoCalls += 1;
          fail(new Error('no fix in tests'));
        }
      }
    },
    XMLHttpRequest: FakeXhr,
    // Refresh and retry timers are recorded, never run, so the test process can exit.
    setTimeout: function(fn, ms) {
      var timer = {fn: fn, ms: ms};
      timers.push(timer);
      return timer;
    },
    clearTimeout: function() {}
  };
  var saved = {};
  Object.keys(globals).forEach(function(k) {
    saved[k] = global[k];
    global[k] = globals[k];
  });

  var originalResolve = Module._resolveFilename;
  Module._resolveFilename = function(request) {
    if (request === 'message_keys') return 'message_keys';
    return originalResolve.apply(this, arguments);
  };
  require.cache.message_keys = {id: 'message_keys', filename: 'message_keys', loaded: true,
    exports: {PAYLOAD: 10000, REQUEST: 10001, TOPLINE: 10002}};
  var file = path.join(__dirname, '..', 'src', 'pkjs', 'index.js');
  delete require.cache[require.resolve(file)];
  require(file);
  Module._resolveFilename = originalResolve;

  return {
    listeners: listeners,
    sent: sent,
    requests: requests,
    timers: timers,
    storage: storage,
    geoCalls: function() { return geoCalls; },
    restore: function() {
      Object.keys(saved).forEach(function(k) { global[k] = saved[k]; });
    }
  };
}

function settle() {
  return new Promise(function(resolve) { setImmediate(resolve); });
}

function forecastBody() {
  return fs.readFileSync(path.join(__dirname, 'fixtures', 'austin.json'), 'utf8');
}

test('a watch request records the platform and starts a refresh that sends a frame', async function(t) {
  var realNow = Date.now;
  Date.now = function() { return NOW_MS; };
  var app = boot({watchInfo: null});
  t.after(function() {
    app.restore();
    Date.now = realNow;
  });

  app.listeners.appmessage({payload: {REQUEST: 2}});
  assert.equal(app.storage.getItem('weatherline.platform'), 'gabbro');
  await settle();
  await settle();
  assert.equal(app.requests.length, 1, 'the forecast was requested');
  var xhr = app.requests[0];
  xhr.status = 200;
  xhr.responseText = forecastBody();
  xhr.onload();
  assert.equal(app.sent.length, 1, 'a frame went to the watch');
  var bytes = app.sent[0][10000];
  assert.equal(bytes[1], 2, 'the frame is laid out for the round watch');
});

test('a request during a refresh does not start a second one', async function(t) {
  var realNow = Date.now;
  Date.now = function() { return NOW_MS; };
  var app = boot({watchInfo: {platform: 'emery'}});
  t.after(function() {
    app.restore();
    Date.now = realNow;
  });

  app.listeners.ready();
  await settle();
  await settle();
  assert.equal(app.requests.length, 1);
  app.listeners.appmessage({payload: {REQUEST: 1}});
  await settle();
  await settle();
  assert.equal(app.requests.length, 1, 'still the one forecast request');
});

test('saving settings mid-fetch keeps the fetch and redraws with the new unit', async function(t) {
  var realNow = Date.now;
  Date.now = function() { return NOW_MS; };
  var app = boot({watchInfo: {platform: 'emery'}});
  t.after(function() {
    app.restore();
    Date.now = realNow;
  });

  app.listeners.ready();
  await settle();
  await settle();
  var xhr = app.requests[0];
  app.listeners.webviewclosed({
    response: encodeURIComponent(JSON.stringify({days: 2, unit: 'C', refreshMin: 30, theme: 'light'}))
  });
  xhr.status = 200;
  xhr.responseText = forecastBody();
  xhr.onload();
  assert.equal(app.sent.length, 1, 'the fetch result still reached the watch');
  assert.equal(app.sent[0][10000][2] & 15, 2, 'and it is in Celsius');
  assert.ok(app.sent[0][10000][2] & 0x10, 'with the light theme');
});

function answer(xhr, body) {
  xhr.status = 200;
  xhr.responseText = body;
  xhr.onload();
}

test('a watch request inside the refresh interval sends nothing and checks nothing', async function(t) {
  var realNow = Date.now;
  Date.now = function() { return NOW_MS; };
  var app = boot({watchInfo: {platform: 'emery'}});
  t.after(function() {
    app.restore();
    Date.now = realNow;
  });

  app.listeners.ready();
  await settle();
  await settle();
  answer(app.requests[0], forecastBody());
  answer(app.requests[1], JSON.stringify({city: 'Austin'}));
  assert.equal(app.sent.length, 2, 'the forecast, then the forecast with its place');
  var geo = app.geoCalls();
  var asked = app.requests.length;

  Date.now = function() { return NOW_MS + 10 * 60000; };
  app.listeners.appmessage({payload: {REQUEST: 1}});
  await settle();
  await settle();
  assert.equal(app.geoCalls(), geo, 'no location check');
  assert.equal(app.requests.length, asked, 'no network request');
  assert.equal(app.sent.length, 2, 'and the unchanged frame is not sent again');
});

test('a later fetch for the same place keeps the known place name', async function(t) {
  var realNow = Date.now;
  Date.now = function() { return NOW_MS; };
  var app = boot({watchInfo: {platform: 'emery'}});
  t.after(function() {
    app.restore();
    Date.now = realNow;
  });

  app.listeners.ready();
  await settle();
  await settle();
  answer(app.requests[0], forecastBody());
  assert.ok(app.requests[1].url.indexOf('api-bdc.io/data/reverse-geocode-client') >= 0, 'the first fetch asks for the place');
  answer(app.requests[1], JSON.stringify({city: 'Austin'}));

  Date.now = function() { return NOW_MS + 61 * 60000; };
  app.listeners.appmessage({payload: {REQUEST: 1}});
  await settle();
  await settle();
  assert.equal(app.requests.length, 3, 'an hour on, the forecast is fetched again');
  answer(app.requests[2], forecastBody());
  assert.equal(app.requests.length, 3, 'without asking for the place again');
  var cache = JSON.parse(app.storage.getItem('weatherline.forecast') || 'null');
  if (cache) assert.equal(cache.place, 'Austin');
});

test('a failed place lookup is retried on the next refresh without a new download', async function(t) {
  var realNow = Date.now;
  Date.now = function() { return NOW_MS; };
  var first = boot({watchInfo: {platform: 'emery'}});
  first.listeners.ready();
  await settle();
  await settle();
  answer(first.requests[0], forecastBody());
  var place = first.requests[1];
  place.status = 0;
  place.onerror();
  first.restore();
  var cache = JSON.parse(first.storage.getItem('weatherline.forecast'));
  assert.equal(cache.place, '', 'the failed lookup leaves no name');

  // The face restarts ten minutes later, outside the recently-checked window.
  first.storage.removeItem('weatherline.checkedAt');
  Date.now = function() { return NOW_MS + 10 * 60000; };
  var app = boot({watchInfo: {platform: 'emery'}, storage: first.storage});
  t.after(function() {
    app.restore();
    Date.now = realNow;
  });
  app.listeners.ready();
  await settle();
  await settle();
  assert.equal(app.requests.length, 1, 'only the place is requested');
  assert.ok(app.requests[0].url.indexOf('api-bdc.io/data/reverse-geocode-client') >= 0);
  answer(app.requests[0], JSON.stringify({city: 'Austin'}));
  cache = JSON.parse(app.storage.getItem('weatherline.forecast'));
  assert.equal(cache.place, 'Austin');
});

function readU32(bytes, offset) {
  return (bytes[offset] | (bytes[offset + 1] << 8) | (bytes[offset + 2] << 16) | (bytes[offset + 3] << 24)) >>> 0;
}

test('saving the top line redraws the stored forecast without a new download', async function(t) {
  var realNow = Date.now;
  Date.now = function() { return NOW_MS; };
  var app = boot({watchInfo: {platform: 'emery'}});
  t.after(function() {
    app.restore();
    Date.now = realNow;
  });

  app.listeners.ready();
  await settle();
  await settle();
  answer(app.requests[0], forecastBody());
  var requests = app.requests.length;
  var sent = app.sent.length;
  app.listeners.webviewclosed({
    response: encodeURIComponent(JSON.stringify({
      days: 2, unit: 'F', refreshMin: 60, theme: 'dark', topLine: 'updated'
    }))
  });
  assert.equal(app.requests.length, requests, 'no new download');
  assert.equal(app.sent.length, sent + 1, 'the stored forecast is sent again');
  var bytes = app.sent[app.sent.length - 1][10000];
  assert.equal(bytes[41] & 0x0F, 1, 'the frame asks for the last-updated line');
  assert.equal(readU32(bytes, 4), 1794790800, 'and the fetch time is unchanged');
});

test('saving the top line before any forecast tells the watch on its own', function(t) {
  var app = boot({watchInfo: {platform: 'gabbro'}});
  t.after(function() { app.restore(); });
  app.listeners.webviewclosed({
    response: encodeURIComponent(JSON.stringify({topLine: 'updated', topLeft: 'uv', btAlert: true}))
  });
  assert.equal(app.sent.length, 1);
  assert.equal(app.sent[0][10002], 1 | (5 << 4) | 0x100);
  assert.equal(app.sent[0][10000], undefined);
});

test('a failed refresh keeps the last successful fetch time', async function(t) {
  var realNow = Date.now;
  Date.now = function() { return NOW_MS; };
  var app = boot({watchInfo: {platform: 'emery'}});
  t.after(function() {
    app.restore();
    Date.now = realNow;
  });

  app.listeners.ready();
  await settle();
  await settle();
  answer(app.requests[0], forecastBody());
  answer(app.requests[1], JSON.stringify({city: 'Austin'}));

  Date.now = function() { return NOW_MS + 61 * 60000; };
  app.listeners.appmessage({payload: {REQUEST: 1}});
  await settle();
  await settle();
  var xhr = app.requests[app.requests.length - 1];
  xhr.status = 500;
  xhr.responseText = '';
  xhr.onload();
  var cache = JSON.parse(app.storage.getItem('weatherline.forecast'));
  assert.equal(cache.fetchedAt, 1794790800);
  var bytes = app.sent[app.sent.length - 1][10000];
  assert.equal(readU32(bytes, 4), 1794790800);
});

test('restarting PKJS inside the refresh window skips geo, network, and Bluetooth', async function(t) {
  var realNow = Date.now;
  Date.now = function() { return NOW_MS; };
  var app1 = boot({watchInfo: {platform: 'emery'}});
  t.after(function() {
    app1.restore();
    Date.now = realNow;
  });

  app1.listeners.ready();
  await settle();
  await settle();
  answer(app1.requests[0], forecastBody());
  answer(app1.requests[1], JSON.stringify({city: 'Austin'}));
  var shared = app1.storage;
  app1.restore();

  Date.now = function() { return NOW_MS + 15 * 60000; };
  var app2 = boot({watchInfo: {platform: 'emery'}, storage: shared});
  t.after(function() { app2.restore(); });

  app2.listeners.ready();
  await settle();
  await settle();
  assert.equal(app2.geoCalls(), 0, 'no geolocation on restart within interval');
  assert.equal(app2.requests.length, 0, 'no network fetch on restart within interval');
  assert.equal(app2.sent.length, 0, 'no redundant Bluetooth transfer');
  assert.equal(app2.timers.length, 1, 'refresh timer armed for remaining time');
  assert.equal(app2.timers[0].ms, 45 * 60000);

  // If the watch asks with the need-frame flag (0x80), the phone clears the hash and sends.
  app2.listeners.appmessage({payload: {REQUEST: 0x81}});
  assert.equal(app2.sent.length, 1, 'watch with empty storage receives the cached frame');
});

test('failed AppMessage sends back off exponentially and stop after 3 retries', async function(t) {
  var realNow = Date.now;
  Date.now = function() { return NOW_MS; };
  var app = boot({watchInfo: {platform: 'emery'}, failSend: true});
  t.after(function() {
    app.restore();
    Date.now = realNow;
  });

  app.listeners.ready();
  await settle();
  await settle();
  answer(app.requests[0], forecastBody());
  // First attempt failed and scheduled a 2s retry.
  var retryDelays = [];
  while (true) {
    var retry = app.timers.find(function(x) { return x.ms < 60000; });
    if (!retry) break;
    retryDelays.push(retry.ms);
    app.timers.splice(app.timers.indexOf(retry), 1);
    retry.fn();
  }
  assert.deepEqual(retryDelays, [2000, 4000, 8000]);
  assert.equal(app.sent.length, 4, 'initial attempt plus 3 retries');
});

test('fixed city location skips phone geolocation and resolves a typed city name on save', async function(t) {
  var realNow = Date.now;
  Date.now = function() { return NOW_MS; };
  var app = boot({watchInfo: {platform: 'emery'}});
  t.after(function() {
    app.restore();
    Date.now = realNow;
  });

  app.listeners.webviewclosed({
    response: encodeURIComponent(JSON.stringify({
      locationMode: 'fixed',
      fixedLat: 39.74,
      fixedLon: -104.99,
      fixedPlace: 'Denver'
    }))
  });
  await settle();
  await settle();
  assert.equal(app.geoCalls(), 0, 'no phone geolocation for fixed city');
  assert.equal(app.requests.length, 1, 'forecast fetched directly');
  assert.ok(app.requests[0].url.indexOf('latitude=39.74&longitude=-104.99') > 0);
  answer(app.requests[0], forecastBody());
  assert.equal(app.requests.length, 1, 'no reverse geocode needed when fixedPlace is set');
  assert.equal(app.sent.length, 1);

  // Saving a typed city query resolves it via Open-Meteo geocoding without phone GPS.
  app.listeners.webviewclosed({
    response: encodeURIComponent(JSON.stringify({
      locationMode: 'fixed',
      fixedQuery: 'Seattle'
    }))
  });
  await settle();
  assert.equal(app.geoCalls(), 0, 'still zero phone geolocation');
  assert.ok(app.requests[1].url.indexOf('geocoding-api.open-meteo.com') > 0);
  answer(app.requests[1], JSON.stringify({
    results: [{name: 'Seattle', latitude: 47.61, longitude: -122.33}]
  }));
  await settle();
  assert.ok(app.requests[2].url.indexOf('latitude=47.61&longitude=-122.33') > 0);
});
