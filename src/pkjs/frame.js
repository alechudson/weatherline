var forecast = require('./forecast');

// Version 4 bit-packed hourly fields and carried refreshMin; version 5 adds the freezing
// line row, configurable top-left and top-center slots, Bluetooth disconnect alert flag,
// feels-like temperature mode/values, hourly humidity and UV, and sunrise/sunset events.
var VERSION = 5;
var HEADER_BYTES = 44;
var HOUR_BYTES = 9;
var NO_ROW = 127;
var NO_SPEED = 255;
var NO_DIR = 15;
var NO_FEELS = -128;
var NO_HUMIDITY = 255;
var NO_UV = 15;
var NO_FREEZE = 255;
var DAY_BYTES = 8;
var MAX_SUNS = 8;
// The wind lane is full at this speed.
var WIND_FULL_KMH = 50;
var LIGHT_FLAG = 0x10;
var MONO_FLAG = 0x20;
var BT_ALERT_FLAG = 0x40;
var FEELS_CHART_FLAG = 0x80;

var SLOT_NAMES = ['wind', 'updated', 'steps', 'feels', 'humidity', 'uv', 'sun', 'off'];
var SLOT_CODES = {
  wind: 0,
  updated: 1,
  steps: 2,
  feels: 3,
  humidity: 4,
  uv: 5,
  sun: 6,
  off: 7
};

function slotCode(name, fallback) {
  if (typeof name === 'string' && Object.prototype.hasOwnProperty.call(SLOT_CODES, name)) {
    return SLOT_CODES[name];
  }
  return fallback;
}

function slotName(code, fallback) {
  return code >= 0 && code < SLOT_NAMES.length ? SLOT_NAMES[code] : fallback;
}

function packPrefs(st) {
  var center = slotCode(st && st.topLine, SLOT_CODES.wind);
  var left = slotCode(st && st.topLeft, SLOT_CODES.steps);
  var bt = st && st.btAlert ? 0x100 : 0;
  return (center & 0x0F) | ((left & 0x0F) << 4) | bt;
}

// Both watches use the same centered stack, with the time near the bottom: steps, the current
// wind and battery along the top, a sky lane, the chart (which carries the current
// temperature on its line), wind and precipitation. On emery the time comes next with the date
// and place under it; on gabbro the date and place sit above the time, where the round screen
// is still wide enough for them.
var LAYOUTS = {
  emery: {
    steps: {x: 10, y: 1, w: 60, h: 16},
    windnow: {x: 70, y: 1, w: 60, h: 16},
    battery: {x: 130, y: 1, w: 60, h: 16},
    cloud: {x: 10, y: 20, w: 180, h: 16},
    plot: {x: 10, y: 37, w: 180, h: 78},
    wind: {x: 10, y: 116, w: 180, h: 14},
    precip: {x: 10, y: 131, w: 180, h: 15},
    time: {x: 0, y: 151, w: 200, h: 52},
    meta: {x: 10, y: 206, w: 180, h: 19},
    bluetooth: {x: 14, y: 177, r: 4},
    inset: 14
  },
  gabbro: {
    // The center slot matches Time 2, wide enough for the refresh mark and "06:10". Steps
    // give up a little width; their text already drops the word when the number does not
    // fit, and the battery keeps its.
    steps: {x: 50, y: 36, w: 48, h: 16},
    windnow: {x: 98, y: 36, w: 60, h: 16},
    battery: {x: 158, y: 36, w: 52, h: 16},
    cloud: {x: 36, y: 56, w: 188, h: 15},
    plot: {x: 36, y: 72, w: 188, h: 74},
    wind: {x: 36, y: 147, w: 188, h: 13},
    precip: {x: 36, y: 161, w: 188, h: 15},
    meta: {x: 40, y: 178, w: 180, h: 19},
    time: {x: 60, y: 197, w: 140, h: 34},
    bluetooth: {x: 130, y: 22, r: 4},
    inset: 11
  }
};

function isNum(value) {
  return typeof value === 'number' && isFinite(value);
}

function whole(celsius, unit) {
  if (unit === 'C') return Math.round(celsius);
  return Math.round(celsius * 9 / 5 + 32);
}

function tenths(celsius, unit) {
  if (unit === 'C') return Math.round(celsius * 10);
  return Math.round((celsius * 9 / 5 + 32) * 10);
}

// The line keeps `inset` rows clear at the top and bottom for the day labels.
function tempY(t, lo, hi, plotH, inset) {
  var span = Math.max(1, hi - lo);
  var rows = Math.max(1, plotH - 1 - 2 * inset);
  var y = inset + Math.round((hi - t) * rows / span);
  if (y < 0) y = 0;
  if (y > plotH - 1) y = plotH - 1;
  return y;
}

function columnBox(i, n, plotW) {
  var x = Math.floor(i * plotW / n);
  var w = Math.floor((i + 1) * plotW / n) - x;
  return {x: x, w: w};
}

// A square-root scale keeps a drizzle visible next to a downpour.
function barPx(amount, full, floor, laneH) {
  if (!(amount >= floor)) return 0;
  var px = Math.round(Math.sqrt(Math.min(1, amount / full)) * laneH);
  return Math.max(Math.min(3, laneH), px);
}

function windPx(kmh, laneH) {
  if (!isNum(kmh) || kmh <= 0) return 0;
  return Math.round(Math.min(1, kmh / WIND_FULL_KMH) * laneH);
}

// Wind speed in mph for Fahrenheit and km/h for Celsius, or NO_SPEED when unknown.
function speedByte(kmh, unit) {
  if (!isNum(kmh) || kmh < 0) return NO_SPEED;
  var speed = unit === 'C' ? kmh : kmh / 1.609344;
  return Math.min(254, Math.round(speed));
}

function degByte(celsius, unit) {
  return Math.max(-127, Math.min(127, whole(celsius, unit)));
}

function feelsByte(celsius, unit) {
  if (!isNum(celsius)) return NO_FEELS;
  return degByte(celsius, unit);
}

function humidityByte(pct) {
  if (!isNum(pct)) return NO_HUMIDITY;
  return Math.max(0, Math.min(100, Math.round(pct)));
}

function uvNibble(uv) {
  if (!isNum(uv) || uv < 0) return NO_UV;
  return Math.min(14, Math.round(uv));
}

function plotTempC(hour, useFeels) {
  if (useFeels && isNum(hour.apparentC)) return hour.apparentC;
  return hour.tempC;
}

// The warmer-or-colder band marks a sustained change from the day before. The difference
// from the same hour yesterday is averaged over FRONT_WINDOW_H hours either side, so a single
// cloudy or sunny afternoon does not read as a front. The band is FRONT_PX_PER_C pixels per
// degree of that average, up to FRONT_MAX_PX, and fades in between FRONT_START_C and
// FRONT_FULL_C, so a front swells in and out with the change instead of switching on, and a
// given thickness means the same change whatever the chart's scale.
var FRONT_WINDOW_H = 3;
var FRONT_START_C = 1;
var FRONT_FULL_C = 2;
var FRONT_PX_PER_C = 3;
var FRONT_MAX_PX = 12;

// The edge of the band for an hour: below the line when warmer than yesterday, above it when
// colder, and on the line itself where there is no sustained change, so the watch tapers the
// band to a point there. The edge is held to the rows the line itself can reach, so the band
// never runs into the day labels. NO_ROW when yesterday is unknown.
function frontRow(lineY, bandC, plotH, inset) {
  if (!isNum(bandC)) return NO_ROW;
  var size = Math.abs(bandC);
  var fade = Math.max(0, Math.min(1, (size - FRONT_START_C) / (FRONT_FULL_C - FRONT_START_C)));
  var px = Math.min(FRONT_MAX_PX, Math.round(size * FRONT_PX_PER_C * fade));
  var y = bandC > 0 ? lineY + px : lineY - px;
  var top = Math.min(inset, lineY);
  var bottom = Math.max(plotH - 1 - inset, lineY);
  if (y < top) y = top;
  if (y > bottom) y = bottom;
  return y;
}

// For each hour in `ordered` with a known yesterday, the smoothed change from yesterday in
// degrees C: positive when warmer, negative when colder.
function frontBands(ordered, byTime, useFeels) {
  var deltas = {};
  var i;
  for (i = 0; i < ordered.length; i++) {
    var hour = ordered[i];
    var before = byTime[hour.time - 86400];
    if (!before) continue;
    var a = plotTempC(hour, useFeels);
    var b = plotTempC(before, useFeels);
    if (isNum(a) && isNum(b)) deltas[hour.time] = a - b;
  }
  var bands = {};
  for (i = 0; i < ordered.length; i++) {
    var t = ordered[i].time;
    if (!Object.prototype.hasOwnProperty.call(deltas, t)) continue;
    var sum = 0;
    var count = 0;
    var k;
    for (k = -FRONT_WINDOW_H; k <= FRONT_WINDOW_H; k++) {
      var d = deltas[t + k * 3600];
      if (isNum(d)) {
        sum += d;
        count += 1;
      }
    }
    bands[t] = sum / count;
  }
  return bands;
}

function collectSunEvents(days, nowUnix) {
  var events = [];
  var i;
  for (i = 0; i < days.length; i++) {
    if (isNum(days[i].sunrise)) events.push({time: days[i].sunrise, sunrise: true});
    if (isNum(days[i].sunset)) events.push({time: days[i].sunset, sunrise: false});
  }
  events.sort(function(a, b) { return a.time - b.time; });
  var out = [];
  for (i = 0; i < events.length && out.length < MAX_SUNS; i++) {
    if (events[i].time >= nowUnix - 3600) {
      out.push(((events[i].time >>> 0) & 0xFFFFFFFE) | (events[i].sunrise ? 1 : 0));
    }
  }
  return out;
}

// Direction the wind comes from, snapped to 8 compass octants (0=N..7=NW) or NO_DIR.
function dirOctant(deg) {
  if (!isNum(deg)) return NO_DIR;
  return Math.floor(((((deg % 360) + 360) % 360) + 22.5) / 45) % 8;
}

function localMidnight(unix, offset) {
  return Math.floor((unix + offset) / 86400) * 86400 - offset;
}

function selectHours(hours, offset, days, nowUnix) {
  var start = localMidnight(nowUnix, offset);
  var end = start + days * 86400;
  var chosen = [];
  var i;
  for (i = 0; i < hours.length; i++) {
    var hour = hours[i];
    if (hour.time >= start && hour.time < end) chosen.push(hour);
  }
  if (chosen.length) return chosen;

  var order = [];
  var buckets = {};
  for (i = 0; i < hours.length; i++) {
    var mid = localMidnight(hours[i].time, offset);
    var key = String(mid);
    if (!buckets[key]) {
      buckets[key] = [];
      order.push(mid);
    }
    buckets[key].push(hours[i]);
  }
  if (!order.length) return [];
  var keep = order.slice(Math.max(0, order.length - days));
  var out = [];
  var k;
  for (k = 0; k < keep.length; k++) {
    var list = buckets[String(keep[k])];
    var t;
    for (t = 0; t < list.length; t++) out.push(list[t]);
  }
  return out;
}

function dayGroups(hours, offset) {
  var groups = [];
  var current = null;
  var i;
  for (i = 0; i < hours.length; i++) {
    var mid = localMidnight(hours[i].time, offset);
    if (!current || current.midnight !== mid) {
      current = {midnight: mid, i0: i, i1: i + 1, hours: [hours[i]]};
      groups.push(current);
    } else {
      current.i1 = i + 1;
      current.hours.push(hours[i]);
    }
  }
  return groups;
}

function matchingDay(days, midnight, offset) {
  var key = Math.floor((midnight + offset) / 86400);
  var i;
  for (i = 0; i < days.length; i++) {
    if (Math.floor((days[i].time + offset) / 86400) === key) return days[i];
  }
  return null;
}

function nearestHour(hours, nowUnix) {
  var best = hours[0];
  var bestDist = Math.abs(best.time - nowUnix);
  var i;
  for (i = 1; i < hours.length; i++) {
    var dist = Math.abs(hours[i].time - nowUnix);
    if (dist < bestDist) {
      best = hours[i];
      bestDist = dist;
    }
  }
  return best;
}

function putU8(bytes, o, value) {
  bytes[o] = value & 255;
  return o + 1;
}

function putU32(bytes, o, value) {
  var v = value >>> 0;
  bytes[o] = v & 255;
  bytes[o + 1] = (v >>> 8) & 255;
  bytes[o + 2] = (v >>> 16) & 255;
  bytes[o + 3] = (v >>> 24) & 255;
  return o + 4;
}

function putI16(bytes, o, value) {
  var v = value | 0;
  bytes[o] = v & 255;
  bytes[o + 1] = (v >> 8) & 255;
  return o + 2;
}

function putUtf8(bytes, o, str, max) {
  var end = o + max;
  var i = 0;
  while (i < str.length && o < end) {
    var c = str.charCodeAt(i);
    var cp = c;
    var step = 1;
    if (c >= 0xD800 && c <= 0xDBFF && i + 1 < str.length) {
      var next = str.charCodeAt(i + 1);
      if (next >= 0xDC00 && next <= 0xDFFF) {
        cp = 0x10000 + ((c - 0xD800) << 10) + (next - 0xDC00);
        step = 2;
      }
    }
    var need = cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4;
    if (o + need > end) break;
    if (need === 1) {
      bytes[o++] = cp;
    } else if (need === 2) {
      bytes[o++] = 0xC0 | (cp >> 6);
      bytes[o++] = 0x80 | (cp & 0x3F);
    } else if (need === 3) {
      bytes[o++] = 0xE0 | (cp >> 12);
      bytes[o++] = 0x80 | ((cp >> 6) & 0x3F);
      bytes[o++] = 0x80 | (cp & 0x3F);
    } else {
      bytes[o++] = 0xF0 | (cp >> 18);
      bytes[o++] = 0x80 | ((cp >> 12) & 0x3F);
      bytes[o++] = 0x80 | ((cp >> 6) & 0x3F);
      bytes[o++] = 0x80 | (cp & 0x3F);
    }
    i += step;
  }
}

function readU32(bytes, o) {
  return ((bytes[o] | (bytes[o + 1] << 8) | (bytes[o + 2] << 16) | (bytes[o + 3] << 24)) >>> 0);
}

function readI32(bytes, o) {
  return (bytes[o] | (bytes[o + 1] << 8) | (bytes[o + 2] << 16) | (bytes[o + 3] << 24)) | 0;
}

function readI16(bytes, o) {
  var v = bytes[o] | (bytes[o + 1] << 8);
  if (v & 0x8000) v -= 0x10000;
  return v;
}

function decodeUtf8(bytes, o, len) {
  var out = '';
  var end = o + len;
  var i = o;
  while (i < end && bytes[i] !== 0) {
    var b = bytes[i];
    var cp;
    var need;
    if (b < 0x80) {
      cp = b;
      need = 1;
    } else if ((b & 0xE0) === 0xC0 && i + 1 < end) {
      cp = ((b & 0x1F) << 6) | (bytes[i + 1] & 0x3F);
      need = 2;
    } else if ((b & 0xF0) === 0xE0 && i + 2 < end) {
      cp = ((b & 0x0F) << 12) | ((bytes[i + 1] & 0x3F) << 6) | (bytes[i + 2] & 0x3F);
      need = 3;
    } else if ((b & 0xF8) === 0xF0 && i + 3 < end) {
      cp = ((b & 0x07) << 18) | ((bytes[i + 1] & 0x3F) << 12)
        | ((bytes[i + 2] & 0x3F) << 6) | (bytes[i + 3] & 0x3F);
      need = 4;
    } else {
      break;
    }
    if (cp < 0x10000) {
      out += String.fromCharCode(cp);
    } else {
      var u = cp - 0x10000;
      out += String.fromCharCode(0xD800 + (u >> 10), 0xDC00 + (u & 0x3FF));
    }
    i += need;
  }
  return out;
}

function buildFrame(forecastValue, settings, platform, nowUnix) {
  var layout = LAYOUTS[platform];
  if (!layout || !forecastValue || !settings) return null;
  if (!isNum(forecastValue.utcOffsetSeconds)) return null;
  var hoursIn = forecastValue.hours;
  if (!hoursIn || !hoursIn.length) return null;
  var daysCount = settings.days;
  var unit = settings.unit;
  var useFeels = settings.tempMode === 'feels';
  var offset = forecastValue.utcOffsetSeconds;
  var ordered = hoursIn.slice().sort(function(a, b) { return a.time - b.time; });
  var selected = selectHours(ordered, offset, daysCount, nowUnix);
  if (!selected.length) return null;
  if (selected.length > 255) return null;

  // The warmer-or-colder band beside the line, from the change since the same hour yesterday.
  // It does not touch the chart's scale.
  var byTime = {};
  var i;
  for (i = 0; i < ordered.length; i++) byTime[ordered[i].time] = ordered[i];
  var bands = frontBands(ordered, byTime, useFeels);

  var lo = tenths(plotTempC(selected[0], useFeels), unit);
  var hi = lo;
  for (i = 0; i < selected.length; i++) {
    var sample = tenths(plotTempC(selected[i], useFeels), unit);
    if (sample < lo) lo = sample;
    if (sample > hi) hi = sample;
  }
  while (hi - lo < 10) {
    lo -= 1;
    hi += 1;
  }

  var plotH = layout.plot.h;
  var plotW = layout.plot.w;
  var laneH = layout.precip.h;
  var freezeTenths = unit === 'C' ? 0 : 320;
  var freezeY = (lo <= freezeTenths && freezeTenths <= hi)
    ? tempY(freezeTenths, lo, hi, plotH, layout.inset || 0)
    : NO_FREEZE;
  var n = selected.length;
  var hourRecs = [];
  for (i = 0; i < n; i++) {
    var hour = selected[i];
    var hTemp = plotTempC(hour, useFeels);
    var lineY = tempY(tenths(hTemp, unit), lo, hi, plotH, layout.inset || 0);
    hourRecs.push({
      tempY: lineY,
      prevY: frontRow(lineY, bands[hour.time], plotH, layout.inset || 0),
      night: hour.isDay === 1 ? 0 : 1,
      rainH: barPx(hour.rainMm, 4, 0.1, laneH),
      snowH: barPx(hour.snowCm, 1, 0.05, laneH),
      cloudH: Math.round(hour.cloud / 100 * layout.cloud.h),
      windH: windPx(hour.windKmh, layout.wind.h),
      windDir: dirOctant(hour.windDir),
      deg: degByte(hTemp, unit),
      feelsDeg: feelsByte(hour.apparentC, unit),
      windSpeed: speedByte(hour.windKmh, unit),
      humidity: humidityByte(hour.humidity),
      uv: uvNibble(hour.uv)
    });
  }

  var groups = dayGroups(selected, offset);
  var daily = forecastValue.days || [];
  var dayRecs = [];
  var g;
  for (g = 0; g < groups.length; g++) {
    var group = groups[g];
    var x = Math.floor(group.i0 * plotW / n);
    var span = Math.floor(group.i1 * plotW / n) - x;
    var match = useFeels ? null : matchingDay(daily, group.midnight, offset);
    var maxC;
    var minC;
    if (match) {
      maxC = match.maxC;
      minC = match.minC;
    } else {
      maxC = plotTempC(group.hours[0], useFeels);
      minC = maxC;
      var h;
      for (h = 1; h < group.hours.length; h++) {
        var ht = plotTempC(group.hours[h], useFeels);
        if (ht > maxC) maxC = ht;
        if (ht < minC) minC = ht;
      }
    }
    var shifted = new Date((group.midnight + offset) * 1000);
    dayRecs.push({
      x: x,
      span: span,
      maxDeg: whole(maxC, unit),
      minDeg: whole(minC, unit),
      wday: shifted.getUTCDay(),
      mday: shifted.getUTCDate()
    });
  }

  var currentDeg = -32768;
  var activeCurrentC = useFeels && isNum(forecastValue.currentApparentC)
    ? forecastValue.currentApparentC : forecastValue.currentC;
  if (isNum(activeCurrentC)) {
    currentDeg = whole(activeCurrentC, unit);
  } else if (selected.length) {
    currentDeg = whole(plotTempC(nearestHour(selected, nowUnix), useFeels), unit);
  }
  var currentFeels = NO_FEELS;
  if (isNum(forecastValue.currentApparentC)) {
    currentFeels = degByte(forecastValue.currentApparentC, unit);
  } else if (selected.length) {
    currentFeels = feelsByte(nearestHour(selected, nowUnix).apparentC, unit);
  }

  var sunEvents = collectSunEvents(daily, nowUnix);
  var place = forecast.clipPlace(forecastValue.place || '');
  var size = HEADER_BYTES + n * HOUR_BYTES + dayRecs.length * DAY_BYTES + sunEvents.length * 4;
  var bytes = new Uint8Array(size);
  var o = 0;
  o = putU8(bytes, o, VERSION);
  o = putU8(bytes, o, platform === 'gabbro' ? 2 : 1);
  // Low bits: 1 for F, 2 for C. High bits: theme, Bluetooth disconnect alert, feels-like line.
  var themeFlag = settings.theme === 'light' ? LIGHT_FLAG : (settings.theme === 'mono' ? MONO_FLAG : 0);
  var btFlag = settings.btAlert ? BT_ALERT_FLAG : 0;
  var feelsFlag = useFeels ? FEELS_CHART_FLAG : 0;
  o = putU8(bytes, o, (unit === 'C' ? 2 : 1) | themeFlag | btFlag | feelsFlag);
  o = putU8(bytes, o, n);
  o = putU32(bytes, o, forecastValue.fetchedAt || 0);
  o = putU32(bytes, o, selected[0].time);
  o = putU32(bytes, o, selected[0].time + n * 3600);
  o = putU32(bytes, o, offset);
  o = putI16(bytes, o, currentDeg);
  o = putU8(bytes, o, dayRecs.length);
  // Header byte 23 carries the configured refresh interval in minutes so the watch
  // only sends a backstop refresh when the phone has actually missed its window.
  var refreshMin = isNum(settings.refreshMin) && settings.refreshMin >= 1
    ? Math.min(255, Math.round(settings.refreshMin)) : 60;
  o = putU8(bytes, o, refreshMin);
  putUtf8(bytes, o, place, 15);
  o += 16;
  o = putU8(bytes, o, freezeY);
  var centerSlot = slotCode(settings.topLine, SLOT_CODES.wind);
  var leftSlot = slotCode(settings.topLeft, SLOT_CODES.steps);
  o = putU8(bytes, o, (centerSlot & 0x0F) | ((leftSlot & 0x0F) << 4));
  o = putU8(bytes, o, sunEvents.length);
  o = putU8(bytes, o, currentFeels);

  for (i = 0; i < hourRecs.length; i++) {
    var rec = hourRecs[i];
    var uv = rec.uv & 0x0F;
    o = putU8(bytes, o, (rec.tempY & 0x7F) | (((uv >> 2) & 1) << 7));
    o = putU8(bytes, o, (rec.prevY & 0x7F) | (((uv >> 3) & 1) << 7));
    o = putU8(bytes, o, rec.deg);
    o = putU8(bytes, o, rec.windSpeed);
    o = putU8(bytes, o, (rec.rainH & 0x0F) | ((rec.snowH & 0x0F) << 4));
    o = putU8(bytes, o, (rec.cloudH & 0x1F) | ((rec.night ? 1 : 0) << 5) | ((uv & 0x03) << 6));
    o = putU8(bytes, o, (rec.windH & 0x0F) | ((rec.windDir & 0x0F) << 4));
    o = putU8(bytes, o, rec.feelsDeg);
    o = putU8(bytes, o, rec.humidity);
  }
  for (g = 0; g < dayRecs.length; g++) {
    var day = dayRecs[g];
    o = putU8(bytes, o, day.x);
    o = putU8(bytes, o, day.span);
    o = putI16(bytes, o, day.maxDeg);
    o = putI16(bytes, o, day.minDeg);
    o = putU8(bytes, o, day.wday);
    o = putU8(bytes, o, day.mday);
  }
  var s;
  for (s = 0; s < sunEvents.length; s++) {
    o = putU32(bytes, o, sunEvents[s]);
  }
  return bytes;
}

function unpackFrame(bytes) {
  if (!bytes || bytes.length < HEADER_BYTES) return null;
  if (bytes[0] !== VERSION) return null;
  var hourCount = bytes[3];
  var dayCount = bytes[22];
  var refreshMin = bytes[23] || 60;
  var freezeY = bytes[40];
  var slotsByte = bytes[41];
  var sunCount = bytes[42];
  var currentFeels = bytes[43] > 127 ? bytes[43] - 256 : bytes[43];
  var expect = HEADER_BYTES + hourCount * HOUR_BYTES + dayCount * DAY_BYTES + sunCount * 4;
  if (bytes.length !== expect) return null;

  var hours = [];
  var o = HEADER_BYTES;
  var i;
  for (i = 0; i < hourCount; i++) {
    var b0 = bytes[o];
    var b1 = bytes[o + 1];
    var precip = bytes[o + 4];
    var sky = bytes[o + 5];
    var wind = bytes[o + 6];
    var uv = ((sky >> 6) & 3) | (((b0 >> 7) & 1) << 2) | (((b1 >> 7) & 1) << 3);
    hours.push({
      tempY: b0 & 0x7F,
      prevY: b1 & 0x7F,
      deg: bytes[o + 2] > 127 ? bytes[o + 2] - 256 : bytes[o + 2],
      windSpeed: bytes[o + 3],
      rainH: precip & 0x0F,
      snowH: (precip >> 4) & 0x0F,
      cloudH: sky & 0x1F,
      night: (sky >> 5) & 1,
      windH: wind & 0x0F,
      windDir: (wind >> 4) & 0x0F,
      feelsDeg: bytes[o + 7] > 127 ? bytes[o + 7] - 256 : bytes[o + 7],
      humidity: bytes[o + 8],
      uv: uv
    });
    o += HOUR_BYTES;
  }
  var days = [];
  var d;
  for (d = 0; d < dayCount; d++) {
    days.push({
      x: bytes[o],
      span: bytes[o + 1],
      maxDeg: readI16(bytes, o + 2),
      minDeg: readI16(bytes, o + 4),
      wday: bytes[o + 6],
      mday: bytes[o + 7]
    });
    o += DAY_BYTES;
  }
  var sunEvents = [];
  var s;
  for (s = 0; s < sunCount; s++) {
    var raw = readU32(bytes, o);
    sunEvents.push({time: raw & 0xFFFFFFFE, sunrise: (raw & 1) === 1});
    o += 4;
  }

  var unitByte = bytes[2];
  return {
    version: bytes[0],
    platform: bytes[1] === 2 ? 'gabbro' : 'emery',
    unit: (unitByte & 15) === 2 ? 'C' : 'F',
    theme: unitByte & LIGHT_FLAG ? 'light' : (unitByte & MONO_FLAG ? 'mono' : 'dark'),
    btAlert: (unitByte & BT_ALERT_FLAG) !== 0,
    tempMode: (unitByte & FEELS_CHART_FLAG) !== 0 ? 'feels' : 'actual',
    topLine: slotName(slotsByte & 0x0F, 'wind'),
    topLeft: slotName((slotsByte >> 4) & 0x0F, 'steps'),
    hourCount: hourCount,
    fetchedAt: readU32(bytes, 4),
    windowStart: readU32(bytes, 8),
    windowEnd: readU32(bytes, 12),
    utcOffset: readI32(bytes, 16),
    currentDeg: readI16(bytes, 20),
    currentFeels: currentFeels,
    dayCount: dayCount,
    refreshMin: refreshMin,
    freezeY: freezeY,
    sunCount: sunCount,
    place: decodeUtf8(bytes, 24, 16),
    hours: hours,
    days: days,
    sunEvents: sunEvents
  };
}

module.exports = {
  version: VERSION,
  layouts: LAYOUTS,
  slotCode: slotCode,
  packPrefs: packPrefs,
  columnBox: columnBox,
  buildFrame: buildFrame,
  unpackFrame: unpackFrame
};
