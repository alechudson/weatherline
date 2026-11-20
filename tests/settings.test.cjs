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
    days: 2, unit: 'F', refreshMin: 60, theme: 'dark', topLine: 'wind'
  });
  var stored = settings.applyConfig(
    storage,
    encodeURIComponent(JSON.stringify({days: 9, unit: 'C', refreshMin: 30}))
  );
  assert.deepEqual(stored, {days: 2, unit: 'C', refreshMin: 30, theme: 'dark', topLine: 'wind'});
  assert.deepEqual(JSON.parse(storage.getItem('weatherline.settings')), stored);
});

test('an empty config response is a dismissed page', function() {
  var storage = memory();
  settings.writeSettings(storage, {days: 5, unit: 'C', refreshMin: 45, theme: 'light', topLine: 'updated'});
  assert.equal(settings.applyConfig(storage, ''), null);
  assert.equal(settings.applyConfig(storage, null), null);
  assert.deepEqual(settings.readSettings(storage), {
    days: 5, unit: 'C', refreshMin: 45, theme: 'light', topLine: 'updated'
  });
});

test('the config page posts the settings through pebblejs close', function() {
  var page = settings.configPage({days: 3, unit: 'C', refreshMin: 45});
  assert.equal(page.indexOf('data:text/html'), 0);
  var html = decodeURIComponent(page.slice(page.indexOf(',') + 1));
  assert.equal(html.indexOf('name="days"') > 0, true);
  assert.equal(html.indexOf('name="unit"') > 0, true);
  assert.equal(html.indexOf('name="refreshMin"') > 0, true);
  assert.equal(html.indexOf('name="theme"') > 0, true);
  assert.equal(html.indexOf('name="topLine"') > 0, true);
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

test('top line is wind or updated, and anything else falls back to wind', function() {
  var storage = memory();
  var updated = settings.applyConfig(storage, encodeURIComponent(JSON.stringify({topLine: 'updated'})));
  assert.equal(updated.topLine, 'updated');
  var wind = settings.applyConfig(storage, encodeURIComponent(JSON.stringify({topLine: 'wind'})));
  assert.equal(wind.topLine, 'wind');
  var odd = settings.applyConfig(storage, encodeURIComponent(JSON.stringify({topLine: 'clock'})));
  assert.equal(odd.topLine, 'wind');
});
