var test = require('node:test');
var assert = require('node:assert/strict');
var settings = require('../src/pkjs/settings');

function memory() {
  var map = {};
  return {
    getItem: function(name) {
      return Object.prototype.hasOwnProperty.call(map, name) ? map[name] : null;
    },
    setItem: function(name, value) {
      map[name] = String(value);
    }
  };
}

test('empty storage reads the defaults and days 9 is stored as 2', function() {
  var storage = memory();
  assert.deepEqual(settings.readSettings(storage), {
    days: 2,
    unit: 'F',
    tempMode: 'actual',
    refreshMin: 60,
    theme: 'dark',
    topLeft: 'steps',
    topLine: 'wind',
    btAlert: false,
    locationMode: 'auto',
    fixedLat: null,
    fixedLon: null,
    fixedPlace: '',
    fixedQuery: ''
  });
  var stored = settings.applyConfig(
    storage,
    encodeURIComponent(JSON.stringify({days: 9, unit: 'C', refreshMin: 30}))
  );
  assert.equal(stored.days, 2);
  assert.equal(stored.unit, 'C');
  assert.equal(stored.refreshMin, 30);
  assert.equal(stored.theme, 'dark');
  assert.equal(stored.topLine, 'wind');
  assert.deepEqual(JSON.parse(storage.getItem('weatherline.settings')), stored);
});

test('an empty config response is a dismissed page', function() {
  var storage = memory();
  settings.writeSettings(storage, {days: 5, unit: 'C', refreshMin: 45, theme: 'light', topLine: 'updated'});
  assert.equal(settings.applyConfig(storage, ''), null);
  assert.equal(settings.applyConfig(storage, null), null);
  var read = settings.readSettings(storage);
  assert.equal(read.days, 5);
  assert.equal(read.unit, 'C');
  assert.equal(read.refreshMin, 45);
  assert.equal(read.theme, 'light');
  assert.equal(read.topLine, 'updated');
});

test('the config page posts the settings through pebblejs close', function() {
  var page = settings.configPage({days: 3, unit: 'C', refreshMin: 45});
  assert.equal(page.indexOf('data:text/html'), 0);
  var html = decodeURIComponent(page.slice(page.indexOf(',') + 1));
  assert.equal(html.indexOf('name="days"') > 0, true);
  assert.equal(html.indexOf('name="unit"') > 0, true);
  assert.equal(html.indexOf('name="tempMode"') > 0, true);
  assert.equal(html.indexOf('name="refreshMin"') > 0, true);
  assert.equal(html.indexOf('name="theme"') > 0, true);
  assert.equal(html.indexOf('name="topLeft"') > 0, true);
  assert.equal(html.indexOf('name="topLine"') > 0, true);
  assert.equal(html.indexOf('name="btAlert"') > 0, true);
  assert.equal(html.indexOf('name="locationMode"') > 0, true);
  assert.equal(html.indexOf('Wind speed') > 0, true);
  assert.equal(html.indexOf('Last updated') > 0, true);
  assert.equal(html.indexOf('pebblejs://close#') > 0, true);
  assert.equal(html.indexOf('encodeURIComponent(JSON.stringify(payload))') > 0, true);
  assert.equal(html.indexOf('topLine:topLine') > 0, true);
});

test('theme is dark, light or mono, and anything else falls back to dark', function() {
  var storage = memory();
  var light = settings.applyConfig(storage, encodeURIComponent(JSON.stringify({theme: 'light'})));
  assert.equal(light.theme, 'light');
  var mono = settings.applyConfig(storage, encodeURIComponent(JSON.stringify({theme: 'mono'})));
  assert.equal(mono.theme, 'mono');
  var odd = settings.applyConfig(storage, encodeURIComponent(JSON.stringify({theme: 'sepia'})));
  assert.equal(odd.theme, 'dark');
});

test('top slots, tempMode, btAlert, and fixed location normalize safely', function() {
  var storage = memory();
  var saved = settings.applyConfig(storage, encodeURIComponent(JSON.stringify({
    tempMode: 'feels',
    topLeft: 'humidity',
    topLine: 'sun',
    btAlert: true,
    locationMode: 'fixed',
    fixedLat: 39.74,
    fixedLon: -104.99,
    fixedPlace: 'Denver'
  })));
  assert.equal(saved.tempMode, 'feels');
  assert.equal(saved.topLeft, 'humidity');
  assert.equal(saved.topLine, 'sun');
  assert.equal(saved.btAlert, true);
  assert.equal(saved.locationMode, 'fixed');
  assert.equal(saved.fixedLat, 39.74);
  assert.equal(saved.fixedLon, -104.99);
  assert.equal(saved.fixedPlace, 'Denver');

  var bad = settings.applyConfig(storage, encodeURIComponent(JSON.stringify({
    tempMode: 'bogus',
    topLeft: 'clock',
    topLine: 'clock',
    btAlert: 'yes',
    locationMode: 'mars',
    fixedLat: 999,
    fixedLon: 0
  })));
  assert.equal(bad.tempMode, 'actual');
  assert.equal(bad.topLeft, 'steps');
  assert.equal(bad.topLine, 'wind');
  assert.equal(bad.btAlert, false);
  assert.equal(bad.locationMode, 'auto');
  assert.equal(bad.fixedLat, null);
  assert.equal(bad.fixedLon, null);
});

test('refresh interval accepts 30, 45, 60, 120, or 180 minutes and defaults to 60', function() {
  var storage = memory();
  [30, 45, 60, 120, 180].forEach(function(mins) {
    var stored = settings.applyConfig(storage, encodeURIComponent(JSON.stringify({refreshMin: mins})));
    assert.equal(stored.refreshMin, mins);
  });
  var odd = settings.applyConfig(storage, encodeURIComponent(JSON.stringify({refreshMin: 90})));
  assert.equal(odd.refreshMin, 60);
});

test('a saved place name cannot close the config page script', function() {
  var page = decodeURIComponent(settings.configPage({
    locationMode: 'fixed', fixedLat: 1, fixedLon: 2, fixedPlace: 'x</script><b>y'
  }).replace(/^data:text\/html;charset=utf-8,/, ''));
  assert.equal(page.split('</script>').length, 2);
  assert.ok(page.indexOf('var fixedPlace="x\\u003c/script>\\u003cb>y";') >= 0);
});
