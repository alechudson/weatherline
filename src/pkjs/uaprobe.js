// TEMPORARY: checks whether the phone's JS engine lets us set a User-Agent header, which
// MET Norway and the US National Weather Service require. Results go to the phone log
// (`pebble logs`). Remove this file and its call in index.js once the answer is known.

var UA = 'Weatherline/1.0 (+https://github.com/alechudson/weatherline)';

var TARGETS = [
  {name: 'met.no', url: 'https://api.met.no/weatherapi/locationforecast/2.0/compact?lat=51.5&lon=-0.12'},
  {name: 'weather.gov', url: 'https://api.weather.gov/points/39.74,-104.99'}
];

function probeOne(target, setUa, log, done) {
  var xhr = new XMLHttpRequest();
  var note = setUa ? 'custom UA' : 'default UA';
  var threw = null;
  xhr.open('GET', target.url);
  xhr.timeout = 20000;
  if (setUa) {
    try {
      xhr.setRequestHeader('User-Agent', UA);
    } catch (e) {
      threw = String(e && e.message || e);
    }
  }
  var finished = false;
  function end(status, extra) {
    if (finished) return;
    finished = true;
    log('[uaprobe] ' + target.name + ' ' + note + ': status=' + status
      + (threw ? ' setRequestHeader threw: ' + threw : '')
      + (extra ? ' ' + extra : ''));
    done();
  }
  xhr.onload = function() {
    var body = String(xhr.responseText || '');
    end(xhr.status, 'bytes=' + body.length + (xhr.status !== 200 ? ' body=' + body.substr(0, 160) : ''));
  };
  xhr.onerror = function() { end('error'); };
  xhr.ontimeout = function() { end('timeout'); };
  xhr.send();
}

// Runs the requests one after another so the log reads in order.
function run(log) {
  var ua = '';
  try { ua = navigator.userAgent || ''; } catch (e) { ua = ''; }
  log('[uaprobe] engine default User-Agent: ' + (ua || '(none)'));
  var steps = [];
  TARGETS.forEach(function(t) {
    steps.push([t, false]);
    steps.push([t, true]);
  });
  var i = 0;
  function next() {
    if (i >= steps.length) {
      log('[uaprobe] done');
      return;
    }
    var step = steps[i++];
    probeOne(step[0], step[1], log, next);
  }
  next();
}

module.exports = {run: run, UA: UA, TARGETS: TARGETS};
