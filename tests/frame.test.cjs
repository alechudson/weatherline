var test = require('node:test');
var assert = require('node:assert/strict');
var fs = require('fs');
var path = require('path');
var forecast = require('../src/pkjs/forecast');
var frame = require('../src/pkjs/frame');

var NOW = 1794790800;

function fixtureForecast(place) {
  var parsed = forecast.parseForecast(
    fs.readFileSync(path.join(__dirname, 'fixtures', 'austin.json'), 'utf8'),
    NOW
  );
  parsed.place = place || '';
  return parsed;
}

function settings(days, unit) {
  return {days: days, unit: unit, refreshMin: 60};
}

test('a 2-day Fahrenheit frame at the fixture instant has 48 hours and current 52', function() {
  var unpacked = frame.unpackFrame(frame.buildFrame(
    fixtureForecast('Hill'),
    settings(2, 'F'),
    'emery',
    NOW
  ));
  assert.equal(unpacked.hourCount, 48);
  assert.equal(unpacked.currentDeg, 52);
  assert.equal(unpacked.place, 'Hill');
  assert.equal((unpacked.windowStart + unpacked.utcOffset) % 86400, 0);
  assert.equal(unpacked.windowStart, 1794722400);
  assert.equal(unpacked.windowEnd - unpacked.windowStart, 48 * 3600);
});

test('the same instant in Celsius has current 11', function() {
  var unpacked = frame.unpackFrame(frame.buildFrame(
    fixtureForecast('Hill'),
    settings(2, 'C'),
    'gabbro',
    NOW
  ));
  assert.equal(unpacked.currentDeg, 11);
  assert.equal(unpacked.unit, 'C');
  assert.equal(unpacked.platform, 'gabbro');
});

test('rain and snow bar heights follow the lane rules', function() {
  var start = 1794722400;
  var made = {
    fetchedAt: NOW,
    utcOffsetSeconds: -21600,
    currentC: 20,
    place: 'X',
    days: [],
    hours: [
      {time: start, tempC: 20, rainMm: 4, snowCm: 0, cloud: 0, isDay: 1},
      {time: start + 3600, tempC: 20, rainMm: 0.05, snowCm: 0, cloud: 0, isDay: 1},
      {time: start + 7200, tempC: 20, rainMm: 0.1, snowCm: 0, cloud: 0, isDay: 1},
      {time: start + 10800, tempC: 20, rainMm: 0, snowCm: 1, cloud: 0, isDay: 1},
      {time: start + 14400, tempC: 20, rainMm: 1, snowCm: 0.2, cloud: 0, isDay: 1}
    ]
  };
  ['emery', 'gabbro'].forEach(function(platform) {
    var unpacked = frame.unpackFrame(frame.buildFrame(made, settings(1, 'C'), platform, NOW));
    var lane = frame.layouts[platform].precip.h;
    assert.equal(unpacked.hours[0].rainH, lane);
    assert.equal(unpacked.hours[1].rainH, 0);
    assert.ok(unpacked.hours[2].rainH >= 3, "a drizzle stays visible");
    assert.ok(unpacked.hours[2].rainH < unpacked.hours[4].rainH);
    assert.equal(unpacked.hours[3].snowH, lane);
    assert.equal(unpacked.hours[3].rainH, 0);
    assert.ok(unpacked.hours[4].rainH > 0);
    assert.ok(unpacked.hours[4].snowH > 0);
  });
});

test('wind fills the lane at 50 km/h and keeps its direction in 2-degree steps', function() {
  var start = 1794722400;
  function hour(i, kmh, dir) {
    return {time: start + i * 3600, tempC: 20, rainMm: 0, snowCm: 0, cloud: 0, isDay: 1,
      windKmh: kmh, windDir: dir};
  }
  var made = {
    fetchedAt: NOW,
    utcOffsetSeconds: -21600,
    currentC: 20,
    place: 'X',
    days: [],
    hours: [hour(0, 50, 0), hour(1, 80, 359), hour(2, 25, 181), hour(3, 0, 90), hour(4, null, null)]
  };
  ['emery', 'gabbro'].forEach(function(platform) {
    var unpacked = frame.unpackFrame(frame.buildFrame(made, settings(1, 'C'), platform, NOW));
    var lane = frame.layouts[platform].wind.h;
    assert.equal(unpacked.hours[0].windH, lane);
    assert.equal(unpacked.hours[1].windH, lane);
    assert.equal(unpacked.hours[2].windH, Math.round(lane / 2));
    assert.equal(unpacked.hours[3].windH, 0);
    assert.equal(unpacked.hours[4].windH, 0);
    assert.equal(unpacked.hours[0].windDir, 0);
    assert.equal(unpacked.hours[1].windDir, 0);
    assert.equal(unpacked.hours[2].windDir, 91);
    assert.equal(unpacked.hours[3].windDir, 45);
    assert.equal(unpacked.hours[4].windDir, 255);
  });
});

test('each hour carries its temperature and wind speed in the chosen units', function() {
  var start = 1794722400;
  function hour(i, tempC, kmh) {
    return {time: start + i * 3600, tempC: tempC, rainMm: 0, snowCm: 0, cloud: 0, isDay: 1,
      windKmh: kmh, windDir: 90};
  }
  var made = {
    fetchedAt: NOW,
    utcOffsetSeconds: -21600,
    currentC: 20,
    place: 'X',
    days: [],
    hours: [hour(0, 20, 16.09344), hour(1, -40, null), hour(2, 60, 500), hour(3, -100, 0)]
  };
  var f = frame.unpackFrame(frame.buildFrame(made, settings(1, 'F'), 'emery', NOW));
  assert.deepEqual(f.hours.map(function(h) { return h.deg; }), [68, -40, 127, -127]);
  assert.deepEqual(f.hours.map(function(h) { return h.windSpeed; }), [10, 255, 254, 0]);
  var c = frame.unpackFrame(frame.buildFrame(made, settings(1, 'C'), 'emery', NOW));
  assert.deepEqual(c.hours.map(function(h) { return h.deg; }), [20, -40, 60, -100]);
  assert.equal(c.hours[0].windSpeed, 16);
});

test('a flat 20C day keeps every tempY on one row', function() {
  var start = 1794722400;
  var hours = [];
  var i;
  for (i = 0; i < 8; i++) {
    hours.push({time: start + i * 3600, tempC: 20, rainMm: 0, snowCm: 0, cloud: 10, isDay: 1});
  }
  var unpacked = frame.unpackFrame(frame.buildFrame({
    fetchedAt: NOW,
    utcOffsetSeconds: -21600,
    currentC: null,
    place: '',
    days: [],
    hours: hours
  }, settings(1, 'C'), 'emery', NOW));
  assert.ok(unpacked);
  var row = unpacked.hours[0].tempY;
  unpacked.hours.forEach(function(hour) {
    assert.equal(hour.tempY, row);
  });
});

test('unpackFrame matches hour count, current, and place, and a flipped version is null', function() {
  var bytes = frame.buildFrame(fixtureForecast('Creek'), settings(2, 'F'), 'emery', NOW);
  var unpacked = frame.unpackFrame(bytes);
  assert.equal(unpacked.hourCount, 48);
  assert.equal(unpacked.currentDeg, 52);
  assert.equal(unpacked.place, 'Creek');
  bytes[0] = 9;
  assert.equal(frame.unpackFrame(bytes), null);
  bytes[0] = frame.version;
  assert.equal(frame.unpackFrame(bytes.subarray(0, bytes.length - 1)), null);
});

test('an unknown platform is null and column widths stay at least 1', function() {
  assert.equal(frame.buildFrame(fixtureForecast(''), settings(2, 'F'), 'chalk', NOW), null);
  [frame.layouts.emery.plot.w, frame.layouts.gabbro.plot.w].forEach(function(plotW) {
    var n;
    for (n = 1; n <= 120; n++) {
      var i;
      for (i = 0; i < n; i++) {
        var box = frame.columnBox(i, n, plotW);
        assert.ok(box.w >= 1, 'width ' + plotW + ' n ' + n + ' i ' + i);
      }
    }
  });
});

test('the light theme sets a flag beside the unit without changing it', function() {
  var light = frame.unpackFrame(frame.buildFrame(
    fixtureForecast('Hill'),
    {days: 2, unit: 'C', refreshMin: 60, theme: 'light'},
    'emery',
    NOW
  ));
  assert.equal(light.theme, 'light');
  assert.equal(light.unit, 'C');
  var dark = frame.unpackFrame(frame.buildFrame(fixtureForecast('Hill'), settings(2, 'F'), 'emery', NOW));
  assert.equal(dark.theme, 'dark');
  assert.equal(dark.unit, 'F');
});

test('the mono theme sets its own flag and keeps the unit', function() {
  var bytes = frame.buildFrame(fixtureForecast('Hill'),
    {days: 2, unit: 'C', refreshMin: 60, theme: 'mono'}, 'emery', NOW);
  var mono = frame.unpackFrame(bytes);
  assert.equal(mono.theme, 'mono');
  assert.equal(mono.unit, 'C');
  assert.equal(bytes[2], 0x22);
});

test('the top line flag rides with the unit and does not restamp the fetch time', function() {
  var forecastValue = fixtureForecast('Hill');
  forecastValue.fetchedAt = NOW - 3600;
  var bytes = frame.buildFrame(forecastValue,
    {days: 2, unit: 'C', refreshMin: 60, theme: 'light', topLine: 'updated'}, 'emery', NOW);
  var updated = frame.unpackFrame(bytes);
  assert.equal(bytes[2], 0x52);
  assert.equal(updated.topLine, 'updated');
  assert.equal(updated.theme, 'light');
  assert.equal(updated.unit, 'C');
  assert.equal(updated.fetchedAt, NOW - 3600);
  var wind = frame.unpackFrame(frame.buildFrame(forecastValue, settings(2, 'F'), 'emery', NOW));
  assert.equal(wind.topLine, 'wind');
  assert.equal(wind.fetchedAt, NOW - 3600);
});
