var forecast = require('./forecast');

// Version 2 adds yesterday's row to each hour.
var VERSION = 2;
var HOUR_BYTES = 6;
var LIGHT_FLAG = 0x10;

// Both watches use the same centered stack: time, temperature, date and place,
// a sky strip, the chart, precipitation, then steps and battery.
var LAYOUTS = {
  emery: {
    time: {x: 0, y: 2, w: 200, h: 46},
    temp: {x: 0, y: 46, w: 200, h: 30},
    meta: {x: 4, y: 76, w: 192, h: 18},
    cloud: {x: 4, y: 98, w: 192, h: 10},
    plot: {x: 4, y: 109, w: 192, h: 83},
    precip: {x: 4, y: 192, w: 192, h: 16},
    steps: {x: 4, y: 210, w: 92, h: 16},
    battery: {x: 104, y: 210, w: 92, h: 16},
    bluetooth: {x: 100, y: 218, r: 4},
    inset: 14
  },
  gabbro: {
    time: {x: 55, y: 32, w: 150, h: 40},
    temp: {x: 70, y: 72, w: 120, h: 30},
    meta: {x: 40, y: 102, w: 180, h: 18},
    cloud: {x: 36, y: 124, w: 188, h: 9},
    plot: {x: 36, y: 134, w: 188, h: 58},
    precip: {x: 36, y: 192, w: 188, h: 16},
    steps: {x: 58, y: 210, w: 72, h: 16},
    battery: {x: 150, y: 210, w: 52, h: 16},
    bluetooth: {x: 140, y: 218, r: 4},
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
  var offset = forecastValue.utcOffsetSeconds;
  var ordered = hoursIn.slice().sort(function(a, b) { return a.time - b.time; });
  var selected = selectHours(ordered, offset, daysCount, nowUnix);
  if (!selected.length) return null;
  if (selected.length > 255) return null;

  // Yesterday at the same hour, for the warmer-or-colder fill. It shares the line's scale.
  var byTime = {};
  var i;
  for (i = 0; i < ordered.length; i++) byTime[ordered[i].time] = ordered[i];
  var prev = [];
  for (i = 0; i < selected.length; i++) prev.push(byTime[selected[i].time - 86400] || null);

  var lo = tenths(selected[0].tempC, unit);
  var hi = lo;
  for (i = 0; i < selected.length; i++) {
    var samples = [selected[i].tempC];
    if (prev[i]) samples.push(prev[i].tempC);
    var k;
    for (k = 0; k < samples.length; k++) {
      var sample = tenths(samples[k], unit);
      if (sample < lo) lo = sample;
      if (sample > hi) hi = sample;
    }
  }
  while (hi - lo < 10) {
    lo -= 1;
    hi += 1;
  }

  var plotH = layout.plot.h;
  var plotW = layout.plot.w;
  var laneH = layout.precip.h;
  var n = selected.length;
  var hourRecs = [];
  for (i = 0; i < n; i++) {
    var hour = selected[i];
    hourRecs.push({
      tempY: tempY(tenths(hour.tempC, unit), lo, hi, plotH, layout.inset || 0),
      prevY: prev[i] ? tempY(tenths(prev[i].tempC, unit), lo, hi, plotH, layout.inset || 0) : 255,
      night: hour.isDay === 1 ? 0 : 1,
      rainH: barPx(hour.rainMm, 4, 0.1, laneH),
      snowH: barPx(hour.snowCm, 1, 0.05, laneH),
      cloudH: Math.round(hour.cloud / 100 * layout.cloud.h)
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
    var match = matchingDay(daily, group.midnight, offset);
    var maxC;
    var minC;
    if (match) {
      maxC = match.maxC;
      minC = match.minC;
    } else {
      maxC = group.hours[0].tempC;
      minC = maxC;
      var h;
      for (h = 1; h < group.hours.length; h++) {
        if (group.hours[h].tempC > maxC) maxC = group.hours[h].tempC;
        if (group.hours[h].tempC < minC) minC = group.hours[h].tempC;
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
  if (isNum(forecastValue.currentC)) {
    currentDeg = whole(forecastValue.currentC, unit);
  } else if (selected.length) {
    currentDeg = whole(nearestHour(selected, nowUnix).tempC, unit);
  }

  var place = forecast.clipPlace(forecastValue.place || '');
  var size = 40 + n * HOUR_BYTES + dayRecs.length * 8;
  var bytes = new Uint8Array(size);
  var o = 0;
  o = putU8(bytes, o, VERSION);
  o = putU8(bytes, o, platform === 'gabbro' ? 2 : 1);
  // Low bits: 1 for F, 2 for C. LIGHT_FLAG asks the watch for the light theme.
  o = putU8(bytes, o, (unit === 'C' ? 2 : 1) | (settings.theme === 'light' ? LIGHT_FLAG : 0));
  o = putU8(bytes, o, n);
  o = putU32(bytes, o, forecastValue.fetchedAt || 0);
  o = putU32(bytes, o, selected[0].time);
  o = putU32(bytes, o, selected[0].time + n * 3600);
  o = putU32(bytes, o, offset);
  o = putI16(bytes, o, currentDeg);
  o = putU8(bytes, o, dayRecs.length);
  // The sun count stays in the header at zero; the watch places suns itself.
  o = putU8(bytes, o, 0);
  putUtf8(bytes, o, place, 15);
  o += 16;

  for (i = 0; i < hourRecs.length; i++) {
    var rec = hourRecs[i];
    o = putU8(bytes, o, rec.tempY);
    o = putU8(bytes, o, rec.night);
    o = putU8(bytes, o, rec.rainH);
    o = putU8(bytes, o, rec.snowH);
    o = putU8(bytes, o, rec.cloudH);
    o = putU8(bytes, o, rec.prevY);
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
  return bytes;
}

function unpackFrame(bytes) {
  if (!bytes || bytes.length < 40) return null;
  if (bytes[0] !== VERSION) return null;
  var hourCount = bytes[3];
  var dayCount = bytes[22];
  var sunCount = bytes[23];
  var expect = 40 + hourCount * HOUR_BYTES + dayCount * 8 + sunCount * 4;
  if (bytes.length !== expect) return null;

  var hours = [];
  var o = 40;
  var i;
  for (i = 0; i < hourCount; i++) {
    hours.push({
      tempY: bytes[o],
      night: bytes[o + 1],
      rainH: bytes[o + 2],
      snowH: bytes[o + 3],
      cloudH: bytes[o + 4],
      prevY: bytes[o + 5]
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
    o += 8;
  }
  var suns = [];
  var s;
  for (s = 0; s < sunCount; s++) {
    suns.push({x: bytes[o], y: bytes[o + 1], r: bytes[o + 2]});
    o += 4;
  }

  var unitByte = bytes[2];
  return {
    version: bytes[0],
    platform: bytes[1] === 2 ? 'gabbro' : 'emery',
    unit: (unitByte & 15) === 2 ? 'C' : 'F',
    theme: unitByte & LIGHT_FLAG ? 'light' : 'dark',
    hourCount: hourCount,
    fetchedAt: readU32(bytes, 4),
    windowStart: readU32(bytes, 8),
    windowEnd: readU32(bytes, 12),
    utcOffset: readI32(bytes, 16),
    currentDeg: readI16(bytes, 20),
    dayCount: dayCount,
    sunCount: sunCount,
    place: decodeUtf8(bytes, 24, 16),
    hours: hours,
    days: days,
    suns: suns
  };
}

module.exports = {
  layouts: LAYOUTS,
  columnBox: columnBox,
  buildFrame: buildFrame,
  unpackFrame: unpackFrame
};
