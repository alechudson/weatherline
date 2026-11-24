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

test('wind fills the lane at 50 km/h and snaps direction to 8 compass octants', function() {
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
    assert.equal(unpacked.hours[2].windDir, 4);
    assert.equal(unpacked.hours[3].windDir, 2);
    assert.equal(unpacked.hours[4].windDir, 15);
  });
});

test('v5 packs hours into 9 bytes and carries refreshMin, freezeY, slots, feels, humidity, UV, and sun events', function() {
  var bytes2 = frame.buildFrame(fixtureForecast('Hill'), {days: 2, unit: 'F', refreshMin: 120}, 'emery', NOW);
  assert.equal(bytes2.length, 44 + 48 * 9 + 2 * 8);
  assert.equal(bytes2[23], 120);
  var unpacked2 = frame.unpackFrame(bytes2);
  assert.equal(unpacked2.refreshMin, 120);
  assert.equal(unpacked2.freezeY, 255);
  var bytes5 = frame.buildFrame(fixtureForecast('Hill'), {days: 5, unit: 'F', refreshMin: 180}, 'gabbro', NOW);
  assert.equal(bytes5.length, 44 + 120 * 9 + 5 * 8);
  assert.equal(frame.unpackFrame(bytes5).refreshMin, 180);

  var start = 1794722400;
  var cold = {
    fetchedAt: NOW,
    utcOffsetSeconds: -21600,
    currentC: 2,
    currentApparentC: -3,
    place: 'Frost',
    days: [{time: start, minC: -5, maxC: 5, sunrise: NOW + 1000, sunset: NOW + 36000}],
    hours: [
      {time: start, tempC: -5, apparentC: -9, rainMm: 0, snowCm: 0, cloud: 20, isDay: 0, humidity: 82, uv: 0},
      {time: start + 3600, tempC: 5, apparentC: 1, rainMm: 0, snowCm: 0, cloud: 40, isDay: 1, humidity: 55, uv: 11}
    ]
  };
  var built = frame.buildFrame(cold, {
    days: 1, unit: 'C', refreshMin: 60, tempMode: 'feels', topLeft: 'uv', topLine: 'sun', btAlert: true
  }, 'emery', NOW);
  var u = frame.unpackFrame(built);
  assert.ok(u.freezeY < frame.layouts.emery.plot.h, 'freezing line sits inside plot when crossing 0C');
  assert.equal(u.tempMode, 'feels');
  assert.equal(u.btAlert, true);
  assert.equal(u.topLeft, 'uv');
  assert.equal(u.topLine, 'sun');
  assert.equal(u.currentDeg, -3);
  assert.equal(u.currentFeels, -3);
  assert.deepEqual(u.hours.map(function(h) { return h.feelsDeg; }), [-9, 1]);
  assert.deepEqual(u.hours.map(function(h) { return h.humidity; }), [82, 55]);
  assert.deepEqual(u.hours.map(function(h) { return h.uv; }), [0, 11]);
  assert.equal(u.sunEvents.length, 2);
  assert.equal(u.sunEvents[0].sunrise, true);
  assert.equal(u.sunEvents[1].sunrise, false);
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

test('the top row slots ride in header byte 41 and do not restamp the fetch time', function() {
  var forecastValue = fixtureForecast('Hill');
  forecastValue.fetchedAt = NOW - 3600;
  var bytes = frame.buildFrame(forecastValue,
    {days: 2, unit: 'C', refreshMin: 60, theme: 'light', topLeft: 'humidity', topLine: 'updated'},
    'emery', NOW);
  var updated = frame.unpackFrame(bytes);
  assert.equal(bytes[2], 0x12);
  assert.equal(updated.topLine, 'updated');
  assert.equal(updated.topLeft, 'humidity');
  assert.equal(updated.theme, 'light');
  assert.equal(updated.unit, 'C');
  assert.equal(updated.fetchedAt, NOW - 3600);
  var wind = frame.unpackFrame(frame.buildFrame(forecastValue, settings(2, 'F'), 'emery', NOW));
  assert.equal(wind.topLine, 'wind');
  assert.equal(wind.topLeft, 'steps');
  assert.equal(wind.fetchedAt, NOW - 3600);
});

// Yesterday and today, hour by hour, with today's temperatures from `today(i)` and yesterday's
// from `yesterday(i)`, for the warmer-or-colder fill.
function twoDayFrame(today, yesterday) {
  var offset = -21600;
  var midnight = Math.floor((NOW + offset) / 86400) * 86400 - offset;
  var hours = [];
  var i;
  for (i = 0; i < 24; i++) {
    hours.push({time: midnight - 86400 + i * 3600, tempC: yesterday(i), rainMm: 0, snowCm: 0, cloud: 0, isDay: 1});
  }
  for (i = 0; i < 24; i++) {
    hours.push({time: midnight + i * 3600, tempC: today(i), rainMm: 0, snowCm: 0, cloud: 0, isDay: 1});
  }
  var unpacked = frame.unpackFrame(frame.buildFrame({
    fetchedAt: NOW,
    utcOffsetSeconds: offset,
    currentC: null,
    place: '',
    days: [],
    hours: hours
  }, settings(1, 'C'), 'emery', NOW));
  assert.ok(unpacked);
  assert.equal(unpacked.hours.length, 24);
  return unpacked;
}

function dayCurve(i) {
  return 10 + 8 * Math.sin((i - 9) * Math.PI / 12);
}

test('a change from yesterday under the threshold leaves the fill empty', function() {
  var unpacked = twoDayFrame(dayCurve, function(i) { return dayCurve(i) - 1; });
  unpacked.hours.forEach(function(hour) {
    assert.equal(hour.prevY, 127);
  });
});

test('a sustained change past the threshold fills every hour on the right side of the line', function() {
  var warmer = twoDayFrame(dayCurve, function(i) { return dayCurve(i) - 3; });
  warmer.hours.forEach(function(hour) {
    assert.notEqual(hour.prevY, 127);
    assert.ok(hour.prevY >= hour.tempY, 'yesterday sits below the line when today is warmer');
  });
  var colder = twoDayFrame(dayCurve, function(i) { return dayCurve(i) + 3; });
  colder.hours.forEach(function(hour) {
    assert.notEqual(hour.prevY, 127);
    assert.ok(hour.prevY <= hour.tempY, 'yesterday sits above the line when today is colder');
  });
});

test('a single warm hour does not read as a front', function() {
  var unpacked = twoDayFrame(function(i) { return i === 14 ? dayCurve(i) + 5 : dayCurve(i); }, dayCurve);
  unpacked.hours.forEach(function(hour) {
    assert.equal(hour.prevY, 127);
  });
});

test('an hour that runs against the change around it is not filled', function() {
  var unpacked = twoDayFrame(function(i) { return i === 14 ? dayCurve(i) - 6 : dayCurve(i); },
    function(i) { return dayCurve(i) - 4; });
  assert.equal(unpacked.hours[14].prevY, 127);
  assert.notEqual(unpacked.hours[13].prevY, 127);
  assert.notEqual(unpacked.hours[15].prevY, 127);
});

test('yesterday does not widen the scale, and its fill stays within the line rows', function() {
  var alone = twoDayFrame(dayCurve, dayCurve);
  var after = twoDayFrame(dayCurve, function(i) { return dayCurve(i) + 25; });
  var inset = frame.layouts.emery.inset;
  var plotH = frame.layouts.emery.plot.h;
  var i;
  for (i = 0; i < 24; i++) {
    assert.equal(after.hours[i].tempY, alone.hours[i].tempY);
    assert.ok(after.hours[i].prevY >= inset);
    assert.ok(after.hours[i].prevY <= plotH - 1 - inset);
    assert.ok(after.hours[i].prevY < after.hours[i].tempY || after.hours[i].tempY === inset);
  }
});
