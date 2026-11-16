var CACHE_KEY = 'weatherline.forecast';

var HOURLY = 'temperature_2m,rain,showers,snowfall,cloud_cover,is_day';

function isNum(value) {
  return typeof value === 'number' && isFinite(value);
}

function isArray(value) {
  return Object.prototype.toString.call(value) === '[object Array]';
}

function utf8Len(cp) {
  if (cp < 0x80) return 1;
  if (cp < 0x800) return 2;
  if (cp < 0x10000) return 3;
  return 4;
}

function clipPlace(str) {
  var s = typeof str === 'string' ? str : '';
  var out = '';
  var used = 0;
  var i = 0;
  while (i < s.length) {
    var c = s.charCodeAt(i);
    var cp = c;
    var step = 1;
    if (c >= 0xD800 && c <= 0xDBFF && i + 1 < s.length) {
      var next = s.charCodeAt(i + 1);
      if (next >= 0xDC00 && next <= 0xDFFF) {
        cp = 0x10000 + ((c - 0xD800) << 10) + (next - 0xDC00);
        step = 2;
      }
    }
    var need = utf8Len(cp);
    if (used + need > 15) break;
    out += s.substr(i, step);
    used += need;
    i += step;
  }
  return out;
}

function cloudByte(value) {
  if (!isNum(value)) return 0;
  var n = Math.round(value);
  if (n < 0) return 0;
  if (n > 100) return 100;
  return n;
}

function parseForecast(text, fetchedAt) {
  var body;
  try {
    body = JSON.parse(text);
  } catch (e) {
    return null;
  }
  if (!body || typeof body !== 'object') return null;
  if (!isNum(body.utc_offset_seconds)) return null;

  var hourly = body.hourly;
  if (!hourly || typeof hourly !== 'object') return null;
  var time = hourly.time;
  var temp = hourly.temperature_2m;
  var rain = hourly.rain;
  var showers = hourly.showers;
  var snow = hourly.snowfall;
  var cloud = hourly.cloud_cover;
  var isDay = hourly.is_day;
  var columns = [time, temp, rain, showers, snow, cloud, isDay];
  if (!isArray(time)) return null;
  var n = time.length;
  var c;
  for (c = 0; c < columns.length; c++) {
    if (!isArray(columns[c]) || columns[c].length !== n) return null;
  }

  var hours = [];
  var i;
  for (i = 0; i < n; i++) {
    if (!isNum(temp[i]) || !isNum(time[i])) continue;
    var rainMm = 0;
    if (isNum(rain[i]) && isNum(showers[i])) rainMm = rain[i] + showers[i];
    hours.push({
      time: time[i],
      tempC: temp[i],
      rainMm: rainMm,
      snowCm: isNum(snow[i]) ? snow[i] : 0,
      cloud: cloudByte(cloud[i]),
      isDay: isDay[i] === 1 ? 1 : 0
    });
  }
  if (!hours.length) return null;

  var days = [];
  var daily = body.daily;
  if (daily && typeof daily === 'object') {
    var dTime = daily.time;
    var dMax = daily.temperature_2m_max;
    var dMin = daily.temperature_2m_min;
    if (isArray(dTime) && isArray(dMax) && isArray(dMin)) {
      var dn = dTime.length;
      if (dMax.length < dn) dn = dMax.length;
      if (dMin.length < dn) dn = dMin.length;
      var j;
      for (j = 0; j < dn; j++) {
        if (!isNum(dTime[j]) || !isNum(dMin[j]) || !isNum(dMax[j])) continue;
        days.push({time: dTime[j], minC: dMin[j], maxC: dMax[j]});
      }
    }
  }

  var currentC = null;
  if (body.current && isNum(body.current.temperature_2m)) {
    currentC = body.current.temperature_2m;
  }

  return {
    fetchedAt: fetchedAt,
    utcOffsetSeconds: body.utc_offset_seconds,
    currentC: currentC,
    hours: hours,
    days: days
  };
}

function forecastUrl(lat, lon) {
  return 'https://api.open-meteo.com/v1/forecast?latitude=' + Number(lat)
    + '&longitude=' + Number(lon)
    + '&hourly=' + HOURLY
    + '&daily=temperature_2m_max,temperature_2m_min'
    + '&current=temperature_2m&timezone=auto&timeformat=unixtime'
    + '&forecast_days=5&past_days=1&temperature_unit=celsius&precipitation_unit=mm';
}

function placeUrl(lat, lon) {
  return 'https://api.bigdatacloud.net/data/reverse-geocode-client?latitude='
    + Number(lat) + '&longitude=' + Number(lon) + '&localityLanguage=en';
}

function placeFromBody(text) {
  var body;
  try {
    body = JSON.parse(text);
  } catch (e) {
    return '';
  }
  if (!body || typeof body !== 'object') return '';
  var fields = [body.city, body.locality, body.principalSubdivision];
  var i;
  for (i = 0; i < fields.length; i++) {
    if (typeof fields[i] === 'string' && fields[i].length) return clipPlace(fields[i]);
  }
  return '';
}

function validHour(hour) {
  return !!hour && isNum(hour.time) && isNum(hour.tempC) && isNum(hour.rainMm)
    && isNum(hour.snowCm) && isNum(hour.cloud) && hour.cloud >= 0 && hour.cloud <= 100
    && hour.cloud === Math.round(hour.cloud) && (hour.isDay === 0 || hour.isDay === 1);
}

function validDay(day) {
  return !!day && isNum(day.time) && isNum(day.minC) && isNum(day.maxC);
}

function validCache(value) {
  if (!value || typeof value !== 'object') return false;
  if (!isNum(value.fetchedAt) || !isNum(value.utcOffsetSeconds)) return false;
  if (!(value.currentC === null || isNum(value.currentC))) return false;
  if (!isArray(value.hours) || !value.hours.length) return false;
  var i;
  for (i = 0; i < value.hours.length; i++) {
    if (!validHour(value.hours[i])) return false;
  }
  if (!isArray(value.days)) return false;
  var j;
  for (j = 0; j < value.days.length; j++) {
    if (!validDay(value.days[j])) return false;
  }
  if (!isNum(value.lat) || !isNum(value.lon)) return false;
  if (typeof value.place !== 'string') return false;
  return true;
}

function readCache(storage) {
  var text = storage.getItem(CACHE_KEY);
  if (!text) return null;
  var value;
  try {
    value = JSON.parse(text);
  } catch (e) {
    return null;
  }
  if (!validCache(value)) return null;
  return value;
}

function writeCache(storage, value) {
  storage.setItem(CACHE_KEY, JSON.stringify(value));
}

module.exports = {
  clipPlace: clipPlace,
  parseForecast: parseForecast,
  forecastUrl: forecastUrl,
  placeUrl: placeUrl,
  placeFromBody: placeFromBody,
  readCache: readCache,
  writeCache: writeCache
};
