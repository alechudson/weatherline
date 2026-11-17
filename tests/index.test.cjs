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
  var geoCalls = 0;
  var storage = memory();
  var cachedFix = JSON.stringify({lat: 30, lon: -97, at: NOW_MS});
  storage.setItem('weatherline.fix', cachedFix);

  function FakeXhr() {
    requests.push(this);
  }
  FakeXhr.prototype.open = function(method, url) { this.url = url; };
  FakeXhr.prototype.send = function() {};

  var globals = {
    Pebble: {
      addEventListener: function(name, fn) { listeners[name] = fn; },
      getActiveWatchInfo: function() { return options.watchInfo || null; },
      sendAppMessage: function(dict, ok) { sent.push(dict); ok(); },
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
    setTimeout: function() { return {}; },
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
    exports: {PAYLOAD: 10000, REQUEST: 10001}};
  var file = path.join(__dirname, '..', 'src', 'pkjs', 'index.js');
  delete require.cache[require.resolve(file)];
  require(file);
  Module._resolveFilename = originalResolve;

  return {
    listeners: listeners,
    sent: sent,
    requests: requests,
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
  assert.ok(app.requests[1].url.indexOf('bigdatacloud') >= 0, 'the first fetch asks for the place');
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
