var test = require('node:test');
var assert = require('node:assert/strict');
var locate = require('../src/pkjs/locate');

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

function position(lat, lon, timestamp) {
  var pos = {coords: {latitude: lat, longitude: lon}};
  if (timestamp !== undefined) pos.timestamp = timestamp;
  return pos;
}

test('resolveFix does not call GPS when the network fix succeeds', async function() {
  var calls = [];
  var result = await locate.resolveFix({
    storage: memory(),
    nowMs: 1.7e12,
    geolocate: function(high) {
      calls.push(high);
      return Promise.resolve(position(4, 5, 10));
    }
  });
  assert.deepEqual(calls, [false]);
  assert.deepEqual(result, {lat: 4, lon: 5});
});

test('resolveFix calls GPS when the network fix fails and uses the cache when both fail', async function() {
  var storage = memory({
    'weatherline.fix': JSON.stringify({lat: 8, lon: 9, at: 1})
  });
  var calls = [];
  var result = await locate.resolveFix({
    storage: storage,
    nowMs: 1.7e12,
    geolocate: function(high) {
      calls.push(high);
      return Promise.reject(new Error('no fix'));
    }
  });
  assert.deepEqual(calls, [false, true]);
  assert.deepEqual(result, {lat: 8, lon: 9});
});

test('a millisecond timestamp older than the maximum age is rejected', async function() {
  var now = 1.7e12;
  var calls = [];
  var storage = memory({
    'weatherline.fix': JSON.stringify({lat: 1, lon: 2, at: 1})
  });
  var result = await locate.resolveFix({
    storage: storage,
    nowMs: now,
    geolocate: function(high) {
      calls.push(high);
      if (!high) {
        return Promise.resolve(position(9, 9, now - 300000 - 60000 - 1));
      }
      return Promise.reject(new Error('gps'));
    }
  });
  assert.deepEqual(calls, [false, true]);
  assert.deepEqual(result, {lat: 1, lon: 2});
});

test('a timestamp below 1e12 is accepted and a nearby fix keeps the cached coordinates', async function() {
  var storage = memory({
    'weatherline.fix': JSON.stringify({lat: 10, lon: 20, at: 1})
  });
  var result = await locate.resolveFix({
    storage: storage,
    nowMs: 5000,
    geolocate: function() {
      return Promise.resolve(position(10.01, 20.01, 1000));
    }
  });
  assert.deepEqual(result, {lat: 10, lon: 20});
  assert.deepEqual(JSON.parse(storage.getItem('weatherline.fix')), {lat: 10, lon: 20, at: 5000});
});
