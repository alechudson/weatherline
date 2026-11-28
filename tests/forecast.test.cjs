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

test('wind is read when present and left empty when the response has none', function() {
  var body = {
    utc_offset_seconds: 0,
    hourly: {
      time: [0, 3600],
      temperature_2m: [10, 11],
      rain: [0, 0],
      showers: [0, 0],
      snowfall: [0, 0],
      cloud_cover: [0, 0],
      is_day: [1, 1],
      wind_speed_10m: [12.5, null],
      wind_direction_10m: [-90, 370]
    }
  };
  var parsed = forecast.parseForecast(JSON.stringify(body), 1);
  assert.equal(parsed.hours[0].windKmh, 12.5);
  assert.equal(parsed.hours[0].windDir, 270);
  assert.equal(parsed.hours[1].windKmh, null);
  assert.equal(parsed.hours[1].windDir, 10);

  body.hourly.wind_speed_10m = [1];
  delete body.hourly.wind_direction_10m;
  parsed = forecast.parseForecast(JSON.stringify(body), 1);
  assert.equal(parsed.hours.length, 2);
  assert.equal(parsed.hours[0].windKmh, null);
  assert.equal(parsed.hours[0].windDir, null);
});

test('a cache written before wind was fetched is still read', function() {
  var storage = memory();
  var parsed = forecast.parseForecast(fixtureText(), 50);
  parsed.lat = 1;
  parsed.lon = 2;
  parsed.place = '';
  parsed.hours.forEach(function(hour) {
    delete hour.windKmh;
    delete hour.windDir;
  });
  forecast.writeCache(storage, parsed);
  assert.deepEqual(forecast.readCache(storage), parsed);

  parsed.hours[0].windKmh = 'fast';
  forecast.writeCache(storage, parsed);
  assert.equal(forecast.readCache(storage), null);
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

test('place names are stored whole and shortened only for the watch', function() {
  var long = 'Township of Clinton Heights Lakeside';
  assert.equal(forecast.placeFromBody(JSON.stringify({city: long})), long);
  assert.equal(forecast.placeFromBody(JSON.stringify({city: '  ', locality: 'Keep'})), 'Keep');
  assert.equal(forecast.clipPlace('Township of Clinton'), 'Clinton');
  assert.equal(forecast.clipPlace('Charter Township of Canton'), 'Canton');
  assert.equal(forecast.clipPlace('City of Ann Arbor'), 'Ann Arbor');
  assert.equal(forecast.clipPlace('Village of'), 'Village of', 'a prefix alone is kept');
  assert.equal(forecast.clipPlace('Rancho Santa Margarita'), 'Rancho Santa');
  assert.equal(forecast.clipPlace('Clinton Township'), 'Clinton', 'cut at a word break');
  assert.equal(forecast.clipPlace('Winston-Salem, NC'), 'Winston-Salem');
  assert.equal(forecast.clipPlace('Llanfairpwllgwyngyll'), 'Llanfairpwllgwy', 'one long word is cut');
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
  assert.equal(url.indexOf('apparent_temperature') > 0, true);
  assert.equal(url.indexOf('relative_humidity_2m') > 0, true);
  assert.equal(url.indexOf('uv_index') > 0, true);
  assert.equal(url.indexOf('sunrise,sunset') > 0, true);
  assert.equal(url.indexOf('wind_speed_unit=kmh') > 0, true);
});

test('parses apparent temp, humidity, UV, sunrise/sunset, geocoding, and coordinate strings', function() {
  var body = JSON.stringify({
    utc_offset_seconds: -21600,
    current: {temperature_2m: 12, apparent_temperature: 9.4},
    hourly: {
      time: [1794722400],
      temperature_2m: [12],
      apparent_temperature: [9.4],
      rain: [0],
      showers: [0],
      snowfall: [0],
      cloud_cover: [20],
      is_day: [1],
      wind_speed_10m: [15],
      wind_direction_10m: [180],
      relative_humidity_2m: [63.6],
      uv_index: [7.2]
    },
    daily: {
      time: [1794722400],
      temperature_2m_max: [18],
      temperature_2m_min: [5],
      sunrise: [1794747000],
      sunset: [1794785000]
    }
  });
  var parsed = forecast.parseForecast(body, 1794722400);
  assert.equal(parsed.currentApparentC, 9.4);
  assert.equal(parsed.hours[0].apparentC, 9.4);
  assert.equal(parsed.hours[0].humidity, 64);
  assert.equal(parsed.hours[0].uv, 7);
  assert.equal(parsed.days[0].sunrise, 1794747000);
  assert.equal(parsed.days[0].sunset, 1794785000);

  assert.deepEqual(forecast.parseCoords('40.71, -74.00'), {lat: 40.71, lon: -74});
  assert.equal(forecast.parseCoords('95, 0'), null);
  assert.deepEqual(forecast.geocodeFromBody(JSON.stringify({
    results: [{name: 'Denver', latitude: 39.74, longitude: -104.99}]
  })), {lat: 39.74, lon: -104.99, place: 'Denver'});
});
