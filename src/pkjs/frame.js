var forecast = require('./forecast');

var LAYOUTS = {
  emery: {
    steps: {x: 18, y: 0, w: 110, h: 18},
    battery: {x: 128, y: 0, w: 70, h: 18},
    time: {x: 2, y: 18, w: 124, h: 44},
    temp: {x: 120, y: 22, w: 78, h: 32},
    date: {x: 2, y: 62, w: 96, h: 18},
    place: {x: 100, y: 62, w: 98, h: 18},
    cloud: {x: 2, y: 82, w: 196, h: 16},
    plot: {x: 2, y: 98, w: 196, h: 100},
    precip: {x: 2, y: 198, w: 196, h: 28},
    bluetooth: {x: 8, y: 9, r: 4}
  },
  gabbro: {
    time: {x: 55, y: 34, w: 150, h: 42},
    temp: {x: 70, y: 78, w: 120, h: 28},
    meta: {x: 40, y: 108, w: 180, h: 16},
    cloud: {x: 36, y: 126, w: 188, h: 14},
    plot: {x: 36, y: 140, w: 188, h: 52},
    precip: {x: 36, y: 192, w: 188, h: 18},
    steps: {x: 58, y: 212, w: 72, h: 16},
    battery: {x: 150, y: 212, w: 52, h: 16},
    bluetooth: {x: 140, y: 220, r: 4}
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

function tempY(t, lo, hi, plotH) {
  var span = Math.max(1, hi - lo);
  var y = Math.round((hi - t) * (plotH - 1) / span);
  if (y < 0) y = 0;
  if (y > plotH - 1) y = plotH - 1;
  return y;
}

function columnBox(i, n, plotW) {
  var x = Math.floor(i * plotW / n);
  var w = Math.floor((i + 1) * plotW / n) - x;
  return {x: x, w: w};
}

function barPx(amount, full, floor, laneH) {
  if (!(amount >= floor)) return 0;
  var px = Math.round(Math.min(1, amount / full) * laneH);
  return Math.max(1, px);
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

function sunRadius(hourCount) {
  if (hourCount <= 24) return 5;
  if (hourCount <= 48) return 4;
  if (hourCount <= 72) return 3;
  return 2;
}

function collectSuns(hours, layout) {
  var n = hours.length;
  var plotW = layout.plot.w;
  var runs = [];
  var i = 0;
  while (i < n) {
    if (!(hours[i].isDay === 1 && hours[i].cloud < 25)) {
      i += 1;
      continue;
    }
    var j = i + 1;
    while (j < n && hours[j].isDay === 1 && hours[j].cloud < 25) j += 1;
    var x0 = Math.floor(i * plotW / n);
    var x1 = Math.floor(j * plotW / n);
    var width = x1 - x0;
    if (width >= 10) {
      var center = x0 + Math.floor(width / 2);
      runs.push({
        i0: i,
        width: width,
        x: (layout.plot.x + center) - layout.cloud.x,
        y: Math.floor(layout.cloud.h / 2)
      });
    }
    i = j;
  }
  runs.sort(function(a, b) {
    if (a.width !== b.width) return b.width - a.width;
    return a.i0 - b.i0;
  });
  var kept = [];
  var r;
  for (r = 0; r < runs.length && kept.length < 6; r++) {
    var ok = true;
    var k;
    for (k = 0; k < kept.length; k++) {
      if (Math.abs(kept[k].x - runs[r].x) <= 10) ok = false;
    }
    if (ok) kept.push(runs[r]);
  }
  var radius = sunRadius(n);
  var suns = [];
  for (r = 0; r < kept.length; r++) {
    suns.push({x: kept[r].x, y: kept[r].y, r: radius});
  }
  return suns;
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

  var lo = tenths(selected[0].tempC, unit);
  var hi = lo;
  var i;
  for (i = 1; i < selected.length; i++) {
    var sample = tenths(selected[i].tempC, unit);
    if (sample < lo) lo = sample;
    if (sample > hi) hi = sample;
  }
  var pad = Math.round((hi - lo) * 0.08);
  lo -= pad;
  hi += pad;
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
      tempY: tempY(tenths(hour.tempC, unit), lo, hi, plotH),
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

  var suns = collectSuns(selected, layout);
  var currentDeg = -32768;
  if (isNum(forecastValue.currentC)) {
    currentDeg = whole(forecastValue.currentC, unit);
  } else if (selected.length) {
    currentDeg = whole(nearestHour(selected, nowUnix).tempC, unit);
  }

  var place = forecast.clipPlace(forecastValue.place || '');
  var size = 40 + n * 5 + dayRecs.length * 8 + suns.length * 4;
  var bytes = new Uint8Array(size);
  var o = 0;
  o = putU8(bytes, o, 1);
  o = putU8(bytes, o, platform === 'gabbro' ? 2 : 1);
  o = putU8(bytes, o, unit === 'C' ? 2 : 1);
  o = putU8(bytes, o, n);
  o = putU32(bytes, o, forecastValue.fetchedAt || 0);
  o = putU32(bytes, o, selected[0].time);
  o = putU32(bytes, o, selected[0].time + n * 3600);
  o = putU32(bytes, o, offset);
  o = putI16(bytes, o, currentDeg);
  o = putU8(bytes, o, dayRecs.length);
  o = putU8(bytes, o, suns.length);
  putUtf8(bytes, o, place, 15);
  o += 16;

  for (i = 0; i < hourRecs.length; i++) {
    var rec = hourRecs[i];
    o = putU8(bytes, o, rec.tempY);
    o = putU8(bytes, o, rec.night);
    o = putU8(bytes, o, rec.rainH);
    o = putU8(bytes, o, rec.snowH);
    o = putU8(bytes, o, rec.cloudH);
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
  for (s = 0; s < suns.length; s++) {
    o = putU8(bytes, o, suns[s].x);
    o = putU8(bytes, o, suns[s].y);
    o = putU8(bytes, o, suns[s].r);
    o = putU8(bytes, o, 0);
  }
  return bytes;
}

function unpackFrame(bytes) {
  if (!bytes || bytes.length < 40) return null;
  if (bytes[0] !== 1) return null;
  var hourCount = bytes[3];
  var dayCount = bytes[22];
  var sunCount = bytes[23];
  var expect = 40 + hourCount * 5 + dayCount * 8 + sunCount * 4;
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
      cloudH: bytes[o + 4]
    });
    o += 5;
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
    unit: unitByte === 2 ? 'C' : 'F',
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
