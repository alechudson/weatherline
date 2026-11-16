var test = require('node:test');
var assert = require('node:assert/strict');
var fs = require('fs');
var path = require('path');
var forecast = require('../src/pkjs/forecast');

function memory(initial) {
  var map = {};
  var key;
  for (key in initial || {}) map[key] = initial[key];
  return {
    getItem: function(name) {
      return Object.prototype.hasOwnProperty.call(map, name) ? map[name] : null;
    },
    setItem: function(name, value) {
      map[name] = String(value);
    }
  };
}

function fixtureText() {
  return fs.readFileSync(path.join(__dirname, 'fixtures', 'austin.json'), 'utf8');
}

test('fixture parses to 144 hours, offset -21600, and current 11', function() {
  var parsed = forecast.parseForecast(fixtureText(), 1794790800);
  assert.equal(parsed.hours.length, 144);
  assert.equal(parsed.utcOffsetSeconds, -21600);
  assert.equal(parsed.currentC, 11);
  assert.equal(parsed.fetchedAt, 1794790800);
});

test('showers are added to rain', function() {
  var body = JSON.stringify({
    utc_offset_seconds: 0,
    current: {temperature_2m: 1},
    hourly: {
      time: [0],
      temperature_2m: [10],
      rain: [0.5],
      showers: [1.5],
      snowfall: [0],
      cloud_cover: [0],
      is_day: [1]
    },
    daily: {time: [], temperature_2m_max: [], temperature_2m_min: []}
  });
  var parsed = forecast.parseForecast(body, 1);
  assert.equal(parsed.hours[0].rainMm, 2);
});

test('mismatched hourly lengths, a non-finite offset, and truncated JSON are null', function() {
  var parsed = JSON.parse(fixtureText());
  parsed.hourly.rain.pop();
  assert.equal(forecast.parseForecast(JSON.stringify(parsed), 1), null);

  parsed = JSON.parse(fixtureText());
  parsed.utc_offset_seconds = null;
  assert.equal(forecast.parseForecast(JSON.stringify(parsed), 1), null);

  assert.equal(forecast.parseForecast('{"utc_offset_seconds":', 1), null);
});

test('place uses the first non-empty name and clips to 15 UTF-8 bytes', function() {
  assert.equal(forecast.placeFromBody(JSON.stringify({locality: 'Town'})), 'Town');
  assert.equal(forecast.placeFromBody(JSON.stringify({
    city: '',
    locality: 'Keep',
    principalSubdivision: 'Skip'
  })), 'Keep');
  assert.equal(forecast.clipPlace('ABCDEFGHIJKLMNOPQRST').length, 15);
  assert.equal(forecast.clipPlace('\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9'), '\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9');
});

test('readCache rejects a shape that is not a forecast and round-trips a real one', function() {
  var storage = memory();
  assert.equal(forecast.readCache(storage), null);
  storage.setItem('weatherline.forecast', '{');
  assert.equal(forecast.readCache(storage), null);
  storage.setItem('weatherline.forecast', '{}');
  assert.equal(forecast.readCache(storage), null);

  var parsed = forecast.parseForecast(fixtureText(), 50);
  parsed.lat = 1;
  parsed.lon = 2;
  parsed.place = '';
  forecast.writeCache(storage, parsed);
  assert.deepEqual(forecast.readCache(storage), parsed);
});

test('forecast URL prints latitude and longitude with Number conversion', function() {
  var url = forecast.forecastUrl('30.5', -97.5);
  assert.equal(url.indexOf('latitude=30.5&longitude=-97.5&') > 0, true);
  assert.equal(url.indexOf('hourly=temperature_2m,rain,showers,snowfall,cloud_cover,is_day') > 0, true);
});
