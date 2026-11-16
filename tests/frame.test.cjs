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
    assert.equal(unpacked.hours[2].rainH, 1);
    assert.equal(unpacked.hours[3].snowH, lane);
    assert.equal(unpacked.hours[3].rainH, 0);
    assert.ok(unpacked.hours[4].rainH > 0);
    assert.ok(unpacked.hours[4].snowH > 0);
  });
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
  bytes[0] = 1;
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
