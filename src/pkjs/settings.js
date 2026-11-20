var SETTINGS_KEY = 'weatherline.settings';

var TOP_LEFT_CHOICES = ['steps', 'feels', 'humidity', 'uv', 'sun', 'off'];
var TOP_LINE_CHOICES = ['wind', 'updated', 'feels', 'humidity', 'uv', 'sun', 'off'];

function isNum(value) {
  return typeof value === 'number' && isFinite(value);
}

function defaults() {
  return {
    days: 2,
    unit: 'F',
    tempMode: 'actual',
    refreshMin: 60,
    theme: 'dark',
    topLeft: 'steps',
    topLine: 'wind',
    btAlert: false,
    locationMode: 'auto',
    fixedLat: null,
    fixedLon: null,
    fixedPlace: '',
    fixedQuery: ''
  };
}

function normalize(raw) {
  var out = defaults();
  if (!raw || typeof raw !== 'object') return out;
  if (raw.days === 1 || raw.days === 2 || raw.days === 3 || raw.days === 5) out.days = raw.days;
  if (raw.unit === 'F' || raw.unit === 'C') out.unit = raw.unit;
  if (raw.tempMode === 'actual' || raw.tempMode === 'feels') out.tempMode = raw.tempMode;
  if (raw.theme === 'dark' || raw.theme === 'light' || raw.theme === 'mono') out.theme = raw.theme;
  if (raw.refreshMin === 30 || raw.refreshMin === 45 || raw.refreshMin === 60
      || raw.refreshMin === 120 || raw.refreshMin === 180) {
    out.refreshMin = raw.refreshMin;
  }
  if (TOP_LEFT_CHOICES.indexOf(raw.topLeft) >= 0) out.topLeft = raw.topLeft;
  // Wind is the default, so a stored settings blob from before this choice stays on wind.
  if (TOP_LINE_CHOICES.indexOf(raw.topLine) >= 0) out.topLine = raw.topLine;
  if (raw.btAlert === true) out.btAlert = true;
  if (raw.locationMode === 'fixed') out.locationMode = 'fixed';
  if (isNum(raw.fixedLat) && isNum(raw.fixedLon)
      && raw.fixedLat >= -90 && raw.fixedLat <= 90
      && raw.fixedLon >= -180 && raw.fixedLon <= 180) {
    out.fixedLat = raw.fixedLat;
    out.fixedLon = raw.fixedLon;
  }
  if (typeof raw.fixedPlace === 'string') out.fixedPlace = raw.fixedPlace.trim().slice(0, 64);
  if (typeof raw.fixedQuery === 'string') out.fixedQuery = raw.fixedQuery.trim().slice(0, 64);
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
  + '.seg input:checked+span{background:#ffaa55;color:#111;font-weight:600}'
  + '.row{display:flex;gap:8px;margin-top:8px}'
  + '.row>div{flex:1}'
  + '.sub{display:block;color:#aaa;font-size:12px;margin:0 0 4px}'
  + 'select,input[type=text]{width:100%;box-sizing:border-box;padding:10px 12px;'
  + 'border:1px solid #444;border-radius:10px;background:#1c1c1c;color:#eee;font:inherit}'
  + '.loc-panel{margin-top:10px;display:none}'
  + '.loc-panel.open{display:block}'
  + '.find-btn{width:auto;margin:0;padding:10px 14px;background:#2a2a2a;color:#eee;border:1px solid #444}'
  + '.status{color:#aaa;font-size:12px;margin:6px 0 0}'
  + '.results{margin-top:8px;display:flex;flex-direction:column;gap:6px}'
  + '.res-btn{width:100%;margin:0;padding:10px 12px;text-align:left;background:#1c1c1c;'
  + 'color:#eee;border:1px solid #444;font-weight:400}'
  + '.res-btn.picked{border-color:#ffaa55;color:#ffaa55}'
  + 'button{width:100%;padding:14px;border:0;border-radius:10px;background:#ffaa55;'
  + 'color:#111;font:inherit;font-weight:600;margin-top:8px}'
  + '.credit{margin-top:24px;color:#777;font-size:12px}';

// JSON for an inline <script>: escaping "<" keeps a value like "</script>" from closing the tag.
function scriptJson(value) {
  return JSON.stringify(value).replace(/</g, '\\u003c');
}

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

function selectBox(label, name, choices, current) {
  var html = '<div><label class="sub">' + escapeHtml(label) + '</label><select name="' + name + '">';
  var i;
  for (i = 0; i < choices.length; i++) {
    html += '<option value="' + escapeHtml(choices[i].value) + '"'
      + (choices[i].value === current ? ' selected' : '') + '>'
      + escapeHtml(choices[i].label) + '</option>';
  }
  return html + '</select></div>';
}

function configPage(settings) {
  var s = normalize(settings);
  var locStatus = s.fixedLat !== null && s.fixedLon !== null
    ? 'Saved: ' + (s.fixedPlace || 'Custom') + ' (' + s.fixedLat.toFixed(2) + ', ' + s.fixedLon.toFixed(2) + ')'
    : 'Enter a city name or lat, lon (e.g. 40.71, -74.00).';
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
    + group('Temperature line', 'Plot actual air temperature or feels-like (apparent) temperature.', 'tempMode', [
      {value: 'actual', label: 'Actual'},
      {value: 'feels', label: 'Feels like'}
    ], s.tempMode)
    + group('Refresh every', 'A longer interval saves watch and phone battery; the watch follows the forecast between updates.', 'refreshMin', [
      {value: 30, label: '30m'},
      {value: 45, label: '45m'},
      {value: 60, label: '1h'},
      {value: 120, label: '2h'},
      {value: 180, label: '3h'}
    ], s.refreshMin)
    + group('Theme', 'The watch screen is lit by daylight, so Light reads best outdoors and Dark suits dim rooms and the backlight. Mono is black, white and gray only.', 'theme', [
      {value: 'dark', label: 'Dark'},
      {value: 'light', label: 'Light'},
      {value: 'mono', label: 'Mono'}
    ], s.theme)
    + '<fieldset><legend>Top row</legend>'
    + '<p class="hint">Pick what appears in the top left and center slots beside the battery.</p>'
    + '<div class="row">'
    + selectBox('Top left', 'topLeft', [
      {value: 'steps', label: 'Steps'},
      {value: 'feels', label: 'Feels like'},
      {value: 'humidity', label: 'Humidity'},
      {value: 'uv', label: 'UV index'},
      {value: 'sun', label: 'Sunrise / sunset'},
      {value: 'off', label: 'Off'}
    ], s.topLeft)
    + selectBox('Top center', 'topLine', [
      {value: 'wind', label: 'Wind speed'},
      {value: 'updated', label: 'Last updated'},
      {value: 'feels', label: 'Feels like'},
      {value: 'humidity', label: 'Humidity'},
      {value: 'uv', label: 'UV index'},
      {value: 'sun', label: 'Sunrise / sunset'},
      {value: 'off', label: 'Off'}
    ], s.topLine)
    + '</div></fieldset>'
    + group('Bluetooth disconnect alert', 'Buzzes briefly if disconnected for 3 seconds (silent during Quiet Time).', 'btAlert', [
      {value: 'off', label: 'Off'},
      {value: 'on', label: 'Buzz'}
    ], s.btAlert ? 'on' : 'off')
    + '<fieldset><legend>Location</legend>'
    + '<p class="hint">Fixed city skips phone GPS completely for better battery life.</p>'
    + '<div class="seg">'
    + radio('locationMode', 'auto', 'Automatic', s.locationMode === 'auto')
    + radio('locationMode', 'fixed', 'Fixed city', s.locationMode === 'fixed')
    + '</div>'
    + '<div id="locPanel" class="loc-panel' + (s.locationMode === 'fixed' ? ' open' : '') + '">'
    + '<div class="row"><input type="text" id="locInput" placeholder="City name or lat, lon" value="'
    + escapeHtml(s.fixedPlace || s.fixedQuery || '') + '">'
    + '<button type="button" id="findBtn" class="find-btn">Find</button></div>'
    + '<p id="locStatus" class="status">' + escapeHtml(locStatus) + '</p>'
    + '<div id="results" class="results"></div>'
    + '</div></fieldset>'
    + '<button type="button" id="save">Save</button></form>'
    + '<p class="credit">Weather and geocoding by Open-Meteo.com (CC BY 4.0). '
    + 'Place names by BigDataCloud.</p><script>'
    + 'var fixedLat=' + scriptJson(s.fixedLat) + ';'
    + 'var fixedLon=' + scriptJson(s.fixedLon) + ';'
    + 'var fixedPlace=' + scriptJson(s.fixedPlace) + ';'
    + 'var dirtyQuery=false;'
    + 'var locInput=document.getElementById("locInput");'
    + 'var locStatus=document.getElementById("locStatus");'
    + 'var resultsEl=document.getElementById("results");'
    + 'var locPanel=document.getElementById("locPanel");'
    + 'document.querySelectorAll("input[name=locationMode]").forEach(function(el){'
    + 'el.addEventListener("change",function(){locPanel.className="loc-panel"+(el.value==="fixed"?" open":"");});'
    + '});'
    + 'locInput.addEventListener("input",function(){dirtyQuery=true;});'
    + 'document.getElementById("findBtn").addEventListener("click",function(){'
    + 'var q=locInput.value.trim();if(!q)return;'
    + 'var m=q.match(/^(-?\\d+(?:\\.\\d+)?)\\s*,\\s*(-?\\d+(?:\\.\\d+)?)$/);'
    + 'if(m){fixedLat=Number(m[1]);fixedLon=Number(m[2]);fixedPlace="";dirtyQuery=false;'
    + 'resultsEl.innerHTML="";locStatus.textContent="Selected: "+fixedLat.toFixed(2)+", "+fixedLon.toFixed(2);return;}'
    + 'locStatus.textContent="Searching...";resultsEl.innerHTML="";'
    + 'fetch("https://geocoding-api.open-meteo.com/v1/search?name="+encodeURIComponent(q)+"&count=5&language=en&format=json")'
    + '.then(function(r){return r.json();}).then(function(d){'
    + 'var list=(d&&d.results)||[];if(!list.length){locStatus.textContent="No matches found. You can still tap Save to try again.";return;}'
    + 'locStatus.textContent="Tap a match:";'
    + 'list.forEach(function(item){'
    + 'var b=document.createElement("button");b.type="button";b.className="res-btn";'
    + 'var label=[item.name,item.admin1,item.country_code].filter(Boolean).join(", ");'
    + 'b.textContent=label;b.addEventListener("click",function(){'
    + 'fixedLat=item.latitude;fixedLon=item.longitude;fixedPlace=item.name||"";dirtyQuery=false;'
    + 'locInput.value=fixedPlace;locStatus.textContent="Selected: "+label+" ("+fixedLat.toFixed(2)+", "+fixedLon.toFixed(2)+")";'
    + 'resultsEl.querySelectorAll("button").forEach(function(x){x.className="res-btn";});b.className="res-btn picked";'
    + '});resultsEl.appendChild(b);});'
    + '}).catch(function(){locStatus.textContent="Search unavailable here; tap Save to resolve the city on your phone.";});'
    + '});'
    + 'document.getElementById("save").addEventListener("click",function(){'
    + 'var days=Number(document.querySelector("input[name=days]:checked").value);'
    + 'var unit=document.querySelector("input[name=unit]:checked").value;'
    + 'var tempMode=document.querySelector("input[name=tempMode]:checked").value;'
    + 'var refreshMin=Number(document.querySelector("input[name=refreshMin]:checked").value);'
    + 'var theme=document.querySelector("input[name=theme]:checked").value;'
    + 'var topLeft=document.querySelector("select[name=topLeft]").value;'
    + 'var topLine=document.querySelector("select[name=topLine]").value;'
    + 'var btAlert=document.querySelector("input[name=btAlert]:checked").value==="on";'
    + 'var locationMode=document.querySelector("input[name=locationMode]:checked").value;'
    + 'var fixedQuery=(locationMode==="fixed"&&(dirtyQuery||fixedLat===null||fixedLon===null))?locInput.value.trim():"";'
    + 'var payload={days:days,unit:unit,tempMode:tempMode,refreshMin:refreshMin,theme:theme,'
    + 'topLeft:topLeft,topLine:topLine,btAlert:btAlert,locationMode:locationMode,'
    + 'fixedLat:fixedLat,fixedLon:fixedLon,fixedPlace:fixedPlace,fixedQuery:fixedQuery};'
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
