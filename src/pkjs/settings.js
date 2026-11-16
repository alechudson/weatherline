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

var STYLE = ''
  + ':root{color-scheme:dark}'
  + 'body{margin:0;padding:20px 16px 32px;background:#111;color:#eee;'
  + 'font:16px/1.4 -apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif}'
  + 'h1{font-size:24px;margin:0 0 4px}'
  + 'p.lede{margin:0 0 20px;color:#999;font-size:14px}'
  + 'fieldset{border:0;margin:0 0 20px;padding:0}'
  + 'legend{font-weight:600;margin:0 0 4px;padding:0}'
  + '.hint{color:#999;font-size:13px;margin:0 0 8px}'
  + '.seg{display:flex;border:1px solid #444;border-radius:10px;overflow:hidden}'
  + '.seg label{flex:1;position:relative;text-align:center}'
  + '.seg input{position:absolute;opacity:0;inset:0;margin:0}'
  + '.seg span{display:block;padding:10px 4px;border-left:1px solid #444}'
  + '.seg label:first-child span{border-left:0}'
  + '.seg input:checked+span{background:#ff9a2e;color:#111;font-weight:600}'
  + 'button{width:100%;padding:14px;border:0;border-radius:10px;background:#ff9a2e;'
  + 'color:#111;font:inherit;font-weight:600;margin-top:8px}'
  + '.credit{margin-top:24px;color:#777;font-size:12px}';

function escapeHtml(text) {
  return String(text).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/"/g, '&quot;');
}

function radio(name, value, label, checked) {
  return '<label><input type="radio" name="' + name + '" value="' + escapeHtml(value) + '"'
    + (checked ? ' checked' : '') + '><span>' + escapeHtml(label) + '</span></label>';
}

function group(legend, hint, name, choices, current) {
  var html = '<fieldset><legend>' + legend + '</legend>'
    + '<p class="hint">' + hint + '</p><div class="seg">';
  var i;
  for (i = 0; i < choices.length; i++) {
    html += radio(name, choices[i].value, choices[i].label, choices[i].value === current);
  }
  return html + '</div></fieldset>';
}

function configPage(settings) {
  var s = normalize(settings);
  var html = '<!DOCTYPE html><html><head><meta charset="utf-8">'
    + '<meta name="viewport" content="width=device-width,initial-scale=1">'
    + '<title>Weatherline</title><style>' + STYLE + '</style></head><body>'
    + '<h1>Weatherline</h1>'
    + '<p class="lede">An hour-by-hour forecast on your wrist.</p><form>'
    + group('Days to show', 'Starts at midnight today.', 'days', [
      {value: 1, label: '1'},
      {value: 2, label: '2'},
      {value: 3, label: '3'},
      {value: 5, label: '5'}
    ], s.days)
    + group('Temperature', 'Changing this redraws without downloading again.', 'unit', [
      {value: 'F', label: '\u00b0F'},
      {value: 'C', label: '\u00b0C'}
    ], s.unit)
    + group('Refresh every', 'A shorter interval uses a little more phone battery.', 'refreshMin', [
      {value: 30, label: '30 min'},
      {value: 45, label: '45 min'},
      {value: 60, label: '1 hour'}
    ], s.refreshMin)
    + '<button type="button" id="save">Save</button></form>'
    + '<p class="credit">Weather data by Open-Meteo.com (CC BY 4.0). '
    + 'Place names by BigDataCloud.</p><script>'
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
