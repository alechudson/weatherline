var SETTINGS_KEY = 'weatherline.settings';

function defaults() {
  return {days: 2, unit: 'F', refreshMin: 60};
}

function normalize(raw) {
  var out = defaults();
  if (!raw || typeof raw !== 'object') return out;
  if (raw.days === 1 || raw.days === 2 || raw.days === 3 || raw.days === 5) out.days = raw.days;
  if (raw.unit === 'F' || raw.unit === 'C') out.unit = raw.unit;
  if (raw.refreshMin === 30 || raw.refreshMin === 45 || raw.refreshMin === 60) {
    out.refreshMin = raw.refreshMin;
  }
  return out;
}

function readSettings(storage) {
  var text = storage.getItem(SETTINGS_KEY);
  if (!text) return defaults();
  try {
    return normalize(JSON.parse(text));
  } catch (e) {
    return defaults();
  }
}

function writeSettings(storage, settings) {
  var clean = normalize(settings);
  storage.setItem(SETTINGS_KEY, JSON.stringify(clean));
  return clean;
}

function radio(name, value, label, checked) {
  return '<label><input type="radio" name="' + name + '" value="' + value + '"'
    + (checked ? ' checked' : '') + '> ' + label + '</label>';
}

function group(legend, name, choices, current) {
  var html = '<fieldset><legend>' + legend + '</legend>';
  var i;
  for (i = 0; i < choices.length; i++) {
    html += radio(name, choices[i].value, choices[i].label, choices[i].value === current);
  }
  return html + '</fieldset>';
}

function configPage(settings) {
  var s = normalize(settings);
  var html = '<!DOCTYPE html><html><head><meta charset="utf-8"><title>Weatherline</title>'
    + '<style>body{font-family:sans-serif;margin:16px}'
    + 'fieldset{margin:0 0 12px;padding:8px}label{display:block;margin:4px 0}</style></head><body><form>'
    + group('Days', 'days', [
      {value: 1, label: '1'},
      {value: 2, label: '2'},
      {value: 3, label: '3'},
      {value: 5, label: '5'}
    ], s.days)
    + group('Unit', 'unit', [
      {value: 'F', label: 'F'},
      {value: 'C', label: 'C'}
    ], s.unit)
    + group('Refresh', 'refreshMin', [
      {value: 30, label: '30'},
      {value: 45, label: '45'},
      {value: 60, label: '60'}
    ], s.refreshMin)
    + '<button type="button" id="save">Save</button></form><script>'
    + 'document.getElementById("save").addEventListener("click",function(){'
    + 'var days=Number(document.querySelector("input[name=days]:checked").value);'
    + 'var unit=document.querySelector("input[name=unit]:checked").value;'
    + 'var refreshMin=Number(document.querySelector("input[name=refreshMin]:checked").value);'
    + 'var payload={days:days,unit:unit,refreshMin:refreshMin};'
    + 'location.href="pebblejs://close#"+encodeURIComponent(JSON.stringify(payload));'
    + '});</script></body></html>';
  return 'data:text/html;charset=utf-8,' + encodeURIComponent(html);
}

function applyConfig(storage, response) {
  if (response == null || response === '') return null;
  var text = String(response);
  var hash = text.indexOf('#');
  if (hash >= 0) text = text.slice(hash + 1);
  if (!text) return null;
  try {
    text = decodeURIComponent(text);
  } catch (e) {
    return null;
  }
  var payload;
  try {
    payload = JSON.parse(text);
  } catch (e2) {
    return null;
  }
  if (!payload || typeof payload !== 'object' || isArray(payload)) return null;
  return writeSettings(storage, payload);
}

function isArray(value) {
  return Object.prototype.toString.call(value) === '[object Array]';
}

module.exports = {
  readSettings: readSettings,
  writeSettings: writeSettings,
  configPage: configPage,
  applyConfig: applyConfig
};
