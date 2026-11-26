import Toybox.ActivityMonitor;
import Toybox.Application.Properties;
import Toybox.Attention;
import Toybox.Graphics;
import Toybox.Lang;
import Toybox.Position;
import Toybox.System;
import Toybox.Time;
import Toybox.Time.Gregorian;
import Toybox.WatchUi;
import Toybox.Weather;

// Instinct 3 Solar 45mm and 50mm share this 176x176 one-bit semi-octagon.
// The upper-right circular subscreen is a separate window: personality puts
// its box at (114, 0, 62, 62), and the bezel hides the gap beside it down
// to about y=74. The full-width band is y=76..134. Layout constants stay
// inside that mask.

const CHART_X = 8;
const CHART_W = 160;
const SKY_Y = 76;
const SKY_H = 13;
const PLOT_Y = 90;
const PLOT_H = 21;
const PRECIP_Y = 112;
const PRECIP_H = 12;
const DATE_Y = 126;
const TIME_TOP = 142;
const STATUS_X = 24;
const STATUS_W = 74;
const MAX_H = 24;

// 0 none, 1 rain (up), 2 snow (down), 3 both.
function precipKind(condition) {
    if (condition == null) {
        return 0;
    }
    if (condition == Weather.CONDITION_SNOW
            || condition == Weather.CONDITION_LIGHT_SNOW
            || condition == Weather.CONDITION_HEAVY_SNOW
            || condition == Weather.CONDITION_CHANCE_OF_SNOW
            || condition == Weather.CONDITION_CLOUDY_CHANCE_OF_SNOW
            || condition == Weather.CONDITION_FLURRIES
            || condition == Weather.CONDITION_ICE_SNOW
            || condition == Weather.CONDITION_ICE) {
        return 2;
    }
    if (condition == Weather.CONDITION_WINTRY_MIX
            || condition == Weather.CONDITION_RAIN_SNOW
            || condition == Weather.CONDITION_LIGHT_RAIN_SNOW
            || condition == Weather.CONDITION_HEAVY_RAIN_SNOW
            || condition == Weather.CONDITION_CHANCE_OF_RAIN_SNOW
            || condition == Weather.CONDITION_CLOUDY_CHANCE_OF_RAIN_SNOW
            || condition == Weather.CONDITION_SLEET) {
        return 3;
    }
    if (condition == Weather.CONDITION_RAIN
            || condition == Weather.CONDITION_LIGHT_RAIN
            || condition == Weather.CONDITION_HEAVY_RAIN
            || condition == Weather.CONDITION_SHOWERS
            || condition == Weather.CONDITION_LIGHT_SHOWERS
            || condition == Weather.CONDITION_HEAVY_SHOWERS
            || condition == Weather.CONDITION_SCATTERED_SHOWERS
            || condition == Weather.CONDITION_CHANCE_OF_SHOWERS
            || condition == Weather.CONDITION_DRIZZLE
            || condition == Weather.CONDITION_THUNDERSTORMS
            || condition == Weather.CONDITION_SCATTERED_THUNDERSTORMS
            || condition == Weather.CONDITION_CHANCE_OF_THUNDERSTORMS
            || condition == Weather.CONDITION_CLOUDY_CHANCE_OF_RAIN
            || condition == Weather.CONDITION_FREEZING_RAIN
            || condition == Weather.CONDITION_TROPICAL_STORM
            || condition == Weather.CONDITION_HURRICANE
            || condition == Weather.CONDITION_HAIL
            || condition == Weather.CONDITION_UNKNOWN_PRECIPITATION
            || condition == Weather.CONDITION_SQUALL) {
        return 1;
    }
    return 0;
}

function cloudFromCondition(condition) {
    if (condition == null) {
        return 40;
    }
    if (condition == Weather.CONDITION_CLEAR
            || condition == Weather.CONDITION_FAIR
            || condition == Weather.CONDITION_PARTLY_CLEAR) {
        return 0;
    }
    if (condition == Weather.CONDITION_MOSTLY_CLEAR
            || condition == Weather.CONDITION_THIN_CLOUDS) {
        return 15;
    }
    if (condition == Weather.CONDITION_PARTLY_CLOUDY) {
        return 45;
    }
    if (condition == Weather.CONDITION_MOSTLY_CLOUDY) {
        return 75;
    }
    if (condition == Weather.CONDITION_CLOUDY) {
        return 100;
    }
    if (precipKind(condition) != 0) {
        return 90;
    }
    if (condition == Weather.CONDITION_FOG
            || condition == Weather.CONDITION_HAZE
            || condition == Weather.CONDITION_HAZY
            || condition == Weather.CONDITION_MIST
            || condition == Weather.CONDITION_SMOKE) {
        return 70;
    }
    return 40;
}

function asFloat(value as Numeric) as Float {
    if (value instanceof Float) {
        return value;
    }
    return value.toFloat();
}

function roundTo(value) {
    if (value >= 0) {
        return (value + 0.5).toNumber();
    }
    return (value - 0.5).toNumber();
}

function propNumber(key as String, fallback as Number) as Number {
    var value = Properties.getValue(key);
    if (value instanceof Number) {
        return value;
    }
    if (value instanceof Float || value instanceof Long) {
        return value.toNumber();
    }
    return fallback;
}

function propFlag(key) {
    return Properties.getValue(key) == true;
}

class WeatherlineView extends WatchUi.WatchFace {

    private var _temp as Array<Number>;
    private var _cloud as Array<Number>;
    private var _pop as Array<Number>;
    private var _kind as Array<Number>;
    private var _when as Array<Moment>;
    private var _day as Array<Number>;
    private var _count;
    private var _has;
    private var _now;
    private var _fg;
    private var _bg;
    private var _asleep;
    private var _burn;
    private var _span;
    private var _f;
    private var _phoneKnown;
    private var _phoneWas;
    private var _loc;
    private var _sunKey;
    private var _rise;
    private var _set;
    private var _lo;
    private var _hi;
    private var _hasMark;
    private var _markX;
    private var _markY;

    function initialize() {
        WatchFace.initialize();
        _temp = new Array<Number>[MAX_H];
        _cloud = new Array<Number>[MAX_H];
        _pop = new Array<Number>[MAX_H];
        _kind = new Array<Number>[MAX_H];
        _when = new Array<Moment>[MAX_H];
        _day = new Array<Number>[MAX_H];
        _count = 0;
        _has = false;
        _now = null;
        _fg = Graphics.COLOR_WHITE;
        _bg = Graphics.COLOR_BLACK;
        _asleep = false;
        _burn = false;
        _span = 24;
        _f = true;
        _phoneKnown = false;
        _phoneWas = true;
        _loc = null;
        _sunKey = -1;
        _rise = null;
        _set = null;
        _lo = 0;
        _hi = 4;
        _hasMark = false;
        _markX = 0;
        _markY = 0;
    }

    function onLayout(dc) {
    }

    function onShow() {
    }

    function onHide() {
    }

    function onExitSleep() {
        _asleep = false;
        WatchUi.requestUpdate();
    }

    function onEnterSleep() {
        _asleep = true;
        WatchUi.requestUpdate();
    }

    function onUpdate(dc) {
        if (dc has :setAntiAlias) {
            dc.setAntiAlias(false);
        }
        readSettings();
        loadForecast();
        notePhone();
        if (_asleep && _burn) {
            drawBurnIn(dc);
        } else {
            dc.setColor(_fg, _bg);
            dc.clear();
            dc.setPenWidth(1);
            drawChart(dc);
            drawStatus(dc);
            drawDate(dc);
            drawTime(dc);
            drawSubscreen(dc);
        }
    }

    function readSettings() {
        var span = propNumber("span", 24);
        if (span != 12 && span != 24) {
            span = 24;
        }
        _span = span;
        var units = propNumber("units", 0);
        if (units == 1) {
            _f = true;
        } else if (units == 2) {
            _f = false;
        } else {
            var settings = System.getDeviceSettings();
            _f = settings.temperatureUnits == System.UNIT_STATUTE;
        }
        if (propFlag("light")) {
            _fg = Graphics.COLOR_BLACK;
            _bg = Graphics.COLOR_WHITE;
        } else {
            _fg = Graphics.COLOR_WHITE;
            _bg = Graphics.COLOR_BLACK;
        }
        _burn = false;
        var device = System.getDeviceSettings();
        if (device has :requiresBurnInProtection && device.requiresBurnInProtection) {
            _burn = true;
        }
    }

    function displayTemp(celsius) {
        var value = asFloat(celsius);
        if (_f) {
            value = (value * 9.0 / 5.0) + 32.0;
        }
        return roundTo(value);
    }

    function loadForecast() {
        _count = 0;
        _has = false;
        _sunKey = -1;
        var info = Position.getInfo();
        _loc = (info != null) ? info.position : null;

        var hourly = Weather.getHourlyForecast();
        if (hourly == null || hourly.size() == 0) {
            readNow();
            return;
        }
        var now = Time.now();
        var start = 0;
        var seen = false;
        for (var i = 0; i < hourly.size(); i++) {
            var hour = hourly[i];
            if (hour == null || hour.forecastTime == null || hour.temperature == null) {
                continue;
            }
            if (hour.forecastTime.compare(now) <= 0) {
                start = i;
                seen = true;
            } else if (seen) {
                break;
            }
        }
        var end = start + _span;
        if (end > hourly.size()) {
            end = hourly.size();
            start = end - _span;
            if (start < 0) {
                start = 0;
            }
        }
        for (var j = start; j < end && _count < MAX_H; j++) {
            var sample = hourly[j];
            if (sample == null || sample.forecastTime == null || sample.temperature == null) {
                continue;
            }
            var tempC = sample.temperature;
            var condition = sample.condition;
            var kind = precipKind(condition);
            var pop = sample.precipitationChance;
            if (pop == null) {
                pop = (kind == 0) ? 0 : 40;
            }
            if (pop < 0) {
                pop = 0;
            }
            if (pop > 100) {
                pop = 100;
            }
            if (kind == 0 && pop >= 20) {
                kind = 1;
            }
            var cloud = (sample has :cloudCover) ? sample.cloudCover : null;
            if (cloud == null) {
                cloud = cloudFromCondition(condition);
            }
            if (cloud < 0) {
                cloud = 0;
            }
            if (cloud > 100) {
                cloud = 100;
            }
            _temp[_count] = displayTemp(tempC);
            _cloud[_count] = cloud;
            _pop[_count] = pop;
            _kind[_count] = kind;
            _when[_count] = sample.forecastTime;
            _day[_count] = hourIsDay(sample.forecastTime) ? 1 : 0;
            _count++;
        }
        _has = _count >= 2;
        readNow();
    }

    function hourIsDay(moment) {
        var info = Gregorian.info(moment, Time.FORMAT_SHORT);
        var key = info.year * 1000 + info.month * 50 + info.day;
        if (key != _sunKey) {
            _sunKey = key;
            _rise = null;
            _set = null;
            if (_loc != null) {
                var noon = Gregorian.moment({
                    :year => info.year,
                    :month => info.month,
                    :day => info.day,
                    :hour => 12
                });
                _rise = Weather.getSunrise(_loc, noon);
                _set = Weather.getSunset(_loc, noon);
            }
        }
        if (_rise == null || _set == null) {
            return info.hour >= 7 && info.hour < 17;
        }
        return moment.compare(_rise) >= 0 && moment.compare(_set) < 0;
    }

    function readNow() {
        _now = null;
        var cur = Weather.getCurrentConditions();
        if (cur != null && cur.temperature != null) {
            var fresh = true;
            if (cur.observationTime != null) {
                var age = Time.now().value() - cur.observationTime.value();
                fresh = age >= -600 && age <= 3600;
            }
            if (fresh) {
                _now = displayTemp(cur.temperature);
                return;
            }
        }
        if (_count < 1) {
            return;
        }
        var now = Time.now().value();
        var best = 0;
        var bestAbs = 2000000000;
        for (var i = 0; i < _count; i++) {
            var delta = _when[i].value() - now;
            if (delta < 0) {
                delta = -delta;
            }
            if (delta < bestAbs) {
                bestAbs = delta;
                best = i;
            }
        }
        _now = _temp[best];
    }

    function notePhone() {
        var settings = System.getDeviceSettings();
        var connected = true;
        if (settings has :phoneConnected && settings.phoneConnected != null) {
            connected = settings.phoneConnected;
        }
        if (_phoneKnown && _phoneWas && !connected && propFlag("btAlert")) {
            if (Attention has :vibrate) {
                Attention.vibrate([
                    new Attention.VibeProfile(70, 120),
                    new Attention.VibeProfile(0, 80),
                    new Attention.VibeProfile(70, 120)
                ]);
            }
        }
        _phoneKnown = true;
        _phoneWas = connected;
    }

    function phoneConnected() {
        var settings = System.getDeviceSettings();
        if (settings has :phoneConnected && settings.phoneConnected != null) {
            return settings.phoneConnected;
        }
        return true;
    }

    function scaleTemps() {
        if (_count < 1) {
            _lo = 0;
            _hi = 4;
            return;
        }
        var lo = _temp[0];
        var hi = _temp[0];
        for (var i = 1; i < _count; i++) {
            if (_temp[i] < lo) {
                lo = _temp[i];
            }
            if (_temp[i] > hi) {
                hi = _temp[i];
            }
        }
        if (hi - lo < 4) {
            var mid = (lo + hi) / 2;
            lo = mid - 2;
            hi = mid + 2;
        }
        _lo = lo;
        _hi = hi;
    }

    function tempY(temp) {
        var span = _hi - _lo;
        if (span < 1) {
            span = 1;
        }
        var usable = PLOT_H - 4;
        var t = (_hi - temp).toFloat() / span.toFloat();
        if (t < 0) {
            t = 0.0;
        }
        if (t > 1) {
            t = 1.0;
        }
        return PLOT_Y + 2 + (t * usable).toNumber();
    }

    function colLeft(i) {
        return CHART_X + (i * CHART_W) / _count;
    }

    function colRight(i) {
        return CHART_X + ((i + 1) * CHART_W) / _count;
    }

    function colMid(i) {
        return (colLeft(i) + colRight(i)) / 2;
    }

    function coverAt(x) {
        if (_count < 1) {
            return 0;
        }
        if (_count == 1) {
            return _cloud[0];
        }
        var pos = (x - CHART_X).toFloat() / CHART_W.toFloat() * (_count - 1).toFloat();
        if (pos < 0) {
            pos = 0.0;
        }
        var i = pos.toNumber();
        if (i >= _count - 1) {
            return _cloud[_count - 1];
        }
        var frac = pos - i.toFloat();
        return _cloud[i] + ((_cloud[i + 1] - _cloud[i]) * frac).toNumber();
    }

    function placeNow() {
        _hasMark = false;
        if (_count < 1) {
            return;
        }
        var now = Time.now().value();
        if (now < _when[0].value()) {
            _markX = colMid(0);
            _markY = tempY(_temp[0]);
            _hasMark = true;
            return;
        }
        for (var i = 0; i < _count - 1; i++) {
            var a = _when[i].value();
            var b = _when[i + 1].value();
            if (now <= b) {
                var denom = b - a;
                if (denom < 1) {
                    denom = 1;
                }
                var frac = (now - a).toFloat() / denom.toFloat();
                if (frac < 0) {
                    frac = 0.0;
                }
                if (frac > 1) {
                    frac = 1.0;
                }
                _markX = colMid(i) + ((colMid(i + 1) - colMid(i)) * frac).toNumber();
                _markY = tempY(_temp[i]) + ((tempY(_temp[i + 1]) - tempY(_temp[i])) * frac).toNumber();
                _hasMark = true;
                return;
            }
        }
        var last = _when[_count - 1].value();
        if (now < last + 3600) {
            _markX = colMid(_count - 1);
            _markY = tempY(_temp[_count - 1]);
            _hasMark = true;
        }
    }

    function strokeFor(temp) {
        if (_asleep) {
            return 1;
        }
        var c = temp;
        if (_f) {
            c = ((temp - 32) * 5) / 9;
        }
        if (c < 0) {
            return 1;
        }
        if (c >= 27) {
            return 3;
        }
        return 2;
    }

    function drawChart(dc) {
        if (!_has) {
            dc.setColor(_fg, Graphics.COLOR_TRANSPARENT);
            dc.drawText(88, PLOT_Y + PLOT_H / 2, Graphics.FONT_SMALL, "No forecast",
                Graphics.TEXT_JUSTIFY_CENTER | Graphics.TEXT_JUSTIFY_VCENTER);
            return;
        }
        scaleTemps();
        placeNow();
        drawClouds(dc);
        drawSuns(dc);
        drawFreezing(dc);
        drawLine(dc);
        drawNow(dc);
        drawPrecip(dc);
        drawDays(dc);
    }

    function drawClouds(dc) {
        dc.setColor(_fg, _fg);
        var floor = SKY_Y + SKY_H - 1;
        var x = CHART_X + 5;
        while (x < CHART_X + CHART_W - 4) {
            var cover = coverAt(x);
            // Clear hours belong to the sun, so the two never occupy one column.
            if (cover < 40) {
                x += 6;
                continue;
            }
            var r = (cover * 4) / 100;
            if (r < 2) {
                r = 2;
            }
            if (r >= 2) {
                var wobble = ((x / 6) % 3) - 1;
                r = r + wobble;
                if (r < 2) {
                    r = 2;
                }
                if (r > 4) {
                    r = 4;
                }
                var cy = floor - r;
                if (cy < SKY_Y + r) {
                    cy = SKY_Y + r;
                }
                dc.fillCircle(x, cy, r);
                var h = floor - cy + 1;
                if (h > 0) {
                    dc.fillRectangle(x - r, cy, r * 2 + 1, h);
                }
            }
            x += 6;
        }
    }

    function drawSuns(dc) {
        if (_count < 1) {
            return;
        }
        dc.setColor(_fg, _fg);
        dc.setPenWidth(1);
        var step = (_count > 12) ? 3 : 2;
        for (var i = 0; i < _count; i += step) {
            if (_cloud[i] >= 40 || _day[i] == 0) {
                continue;
            }
            var x = colMid(i);
            var y = SKY_Y + 4;
            dc.drawCircle(x, y, 2);
            if (!_asleep) {
                dc.drawLine(x - 5, y, x - 3, y);
                dc.drawLine(x + 3, y, x + 5, y);
                dc.drawLine(x, y - 4, x, y - 3);
            }
            dc.fillCircle(x, y, 1);
        }
    }

    function drawFreezing(dc) {
        var freeze = _f ? 32 : 0;
        if (freeze < _lo || freeze > _hi) {
            return;
        }
        var y = tempY(freeze);
        dc.setColor(_fg, _fg);
        for (var x = CHART_X; x < CHART_X + CHART_W; x += 3) {
            dc.drawLine(x, y, x, y);
        }
    }

    function drawLine(dc) {
        dc.setColor(_fg, _fg);
        if (_count < 2) {
            dc.fillCircle(colMid(0), tempY(_temp[0]), 2);
            return;
        }
        for (var i = 0; i < _count - 1; i++) {
            var width = strokeFor((_temp[i] + _temp[i + 1]) / 2);
            dc.setPenWidth(width);
            dc.drawLine(colMid(i), tempY(_temp[i]), colMid(i + 1), tempY(_temp[i + 1]));
        }
        dc.setPenWidth(1);
    }

    function drawNow(dc) {
        if (!_hasMark) {
            return;
        }
        dc.setColor(_fg, _fg);
        dc.setPenWidth(1);
        dc.drawLine(_markX, PLOT_Y + 1, _markX, PLOT_Y + PLOT_H - 2);
        if (_asleep) {
            return;
        }
        var y = _markY;
        if (y < PLOT_Y + 3) {
            y = PLOT_Y + 3;
        }
        if (y > PLOT_Y + PLOT_H - 4) {
            y = PLOT_Y + PLOT_H - 4;
        }
        dc.setColor(_bg, _bg);
        dc.fillCircle(_markX, y, 3);
        dc.setColor(_fg, _fg);
        dc.drawCircle(_markX, y, 3);
    }

    function drawPrecip(dc) {
        var mid = PRECIP_Y + PRECIP_H / 2;
        var half = PRECIP_H / 2 - 1;
        dc.setColor(_fg, _fg);
        dc.drawLine(CHART_X, mid, CHART_X + CHART_W - 1, mid);
        for (var i = 0; i < _count; i++) {
            var chance = _pop[i];
            var kind = _kind[i];
            if (kind == 0 || chance <= 0) {
                continue;
            }
            var h = (chance * half + 50) / 100;
            if (h < 1) {
                h = 1;
            }
            if (h > half) {
                h = half;
            }
            var left = colLeft(i);
            var width = colRight(i) - left - 1;
            if (width < 1) {
                width = 1;
            }
            if (kind == 1 || kind == 3) {
                dc.fillRectangle(left, mid - h, width, h);
            }
            if (kind == 2 || kind == 3) {
                dc.fillRectangle(left, mid + 1, width, h);
            }
        }
    }

    function drawDays(dc) {
        var font = Graphics.FONT_XTINY;
        dc.setPenWidth(1);
        for (var i = 0; i < _count; i++) {
            var info = Gregorian.info(_when[i], Time.FORMAT_SHORT);
            if (info.hour != 0) {
                continue;
            }
            var tick = colMid(i);
            // A tick marks midnight in the plot. The name sits to its left,
            // in the sky, so neither one cuts the temperature line.
            dc.setColor(_fg, _fg);
            dc.drawLine(tick, PLOT_Y + 1, tick, PLOT_Y + PLOT_H - 2);
            if (_hasMark && (tick - _markX < 16 && _markX - tick < 16)) {
                continue;
            }
            var name = weekday(info.day_of_week);
            var w = dc.getTextWidthInPixels(name, font);
            var x = tick - w / 2 - 3;
            if (x - w / 2 < CHART_X) {
                x = CHART_X + w / 2;
            }
            var textY = SKY_Y - 1;
            dc.setColor(_bg, _bg);
            dc.fillRectangle(x - w / 2 - 1, SKY_Y, w + 2, SKY_H);
            dc.setColor(_fg, Graphics.COLOR_TRANSPARENT);
            dc.drawText(x, textY, font, name, Graphics.TEXT_JUSTIFY_CENTER);
        }
    }

    function weekday(index) {
        var names = ["Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"];
        var idx = index - 1;
        if (idx < 0 || idx > 6) {
            return "";
        }
        return names[idx];
    }

    function drawStatus(dc) {
        var font = Graphics.FONT_XTINY;
        dc.setColor(_fg, Graphics.COLOR_TRANSPARENT);
        var steps = readSteps();
        if (steps != null) {
            var label = steps.format("%d") + " steps";
            if (dc.getTextWidthInPixels(label, font) > STATUS_W) {
                label = steps.format("%d");
            }
            dc.drawText(STATUS_X, 20, font, label, Graphics.TEXT_JUSTIFY_LEFT);
        }
        drawBattery(dc, 40);
        if (!phoneConnected()) {
            drawBluetooth(dc, STATUS_X, 58);
        }
    }

    function readSteps() {
        var info = ActivityMonitor.getInfo();
        if (info == null || info.steps == null) {
            return null;
        }
        return info.steps;
    }

    function drawBattery(dc, y) {
        var stats = System.getSystemStats();
        var pct = roundTo(stats.battery);
        if (pct < 0) {
            pct = 0;
        }
        if (pct > 100) {
            pct = 100;
        }
        var x = STATUS_X;
        var iconY = y + 4;
        dc.setColor(_fg, _fg);
        dc.setPenWidth(1);
        dc.drawRectangle(x, iconY, 14, 8);
        dc.fillRectangle(x + 14, iconY + 2, 2, 4);
        var fill = (10 * pct + 50) / 100;
        if (pct > 0 && fill < 1) {
            fill = 1;
        }
        if (pct <= 20 && !stats.charging) {
            dc.fillRectangle(x + 2, iconY + 2, 10, 4);
            dc.setColor(_bg, _bg);
            var empty = 10 - fill;
            if (empty > 0) {
                dc.fillRectangle(x + 2 + fill, iconY + 2, empty, 4);
            }
            dc.setColor(_fg, Graphics.COLOR_TRANSPARENT);
        } else if (fill > 0) {
            dc.fillRectangle(x + 2, iconY + 2, fill, 4);
        }
        var text = pct.format("%d") + "%";
        if (stats.charging) {
            text = text + "+";
        }
        dc.setColor(_fg, Graphics.COLOR_TRANSPARENT);
        dc.drawText(x + 18, y, Graphics.FONT_XTINY, text, Graphics.TEXT_JUSTIFY_LEFT);
    }

    // Shown only while the phone is disconnected. One-bit, so the rune sits
    // in a solid block and reads as the alert.
    function drawBluetooth(dc, x, y) {
        dc.setColor(_fg, _fg);
        dc.fillRectangle(x, y, 9, 11);
        dc.setColor(_bg, _bg);
        dc.drawLine(x + 4, y + 1, x + 4, y + 9);
        dc.drawLine(x + 4, y + 1, x + 7, y + 3);
        dc.drawLine(x + 7, y + 3, x + 2, y + 7);
        dc.drawLine(x + 4, y + 9, x + 7, y + 7);
        dc.drawLine(x + 7, y + 7, x + 2, y + 3);
        dc.setColor(_fg, _fg);
    }

    function drawDate(dc) {
        var info = Gregorian.info(Time.now(), Time.FORMAT_SHORT);
        var text = weekday(info.day_of_week) + " " + info.day.format("%d");
        dc.setColor(_fg, Graphics.COLOR_TRANSPARENT);
        dc.drawText(88, DATE_Y, Graphics.FONT_XTINY, text, Graphics.TEXT_JUSTIFY_CENTER);
    }

    function drawTime(dc) {
        var clock = System.getClockTime();
        var hour = clock.hour;
        var settings = System.getDeviceSettings();
        if (!settings.is24Hour) {
            hour = hour % 12;
            if (hour == 0) {
                hour = 12;
            }
            hour = hour.format("%d");
        } else {
            hour = hour.format("%02d");
        }
        var mins = clock.min.format("%02d");
        // The number font's colon is a single square. Draw the two dots instead.
        var font = Graphics.FONT_NUMBER_MEDIUM;
        var hourW = dc.getTextWidthInPixels(hour, font);
        var minW = dc.getTextWidthInPixels(mins, font);
        var gap = 10;
        var total = hourW + gap + minW;
        var left = 88 - total / 2;
        // Keep the ink off the bottom chamfer. The glyph sits low in the em box.
        if (left < 36) {
            left = 36;
        }
        if (left + total > 140) {
            left = 140 - total;
        }
        dc.setColor(_fg, Graphics.COLOR_TRANSPARENT);
        dc.drawText(left, TIME_TOP, font, hour, Graphics.TEXT_JUSTIFY_LEFT);
        dc.drawText(left + hourW + gap, TIME_TOP, font, mins, Graphics.TEXT_JUSTIFY_LEFT);
        var dotX = left + hourW + gap / 2 - 1;
        dc.setColor(_fg, _fg);
        dc.fillRectangle(dotX, TIME_TOP + 16, 3, 3);
        dc.fillRectangle(dotX, TIME_TOP + 24, 3, 3);
    }

    function drawSubscreen(dc) {
        var box = WatchUi.getSubscreen();
        var cx = 145;
        var cy = 31;
        var limit = 26;
        if (box != null) {
            cx = box.x + box.width / 2;
            cy = box.y + box.height / 2;
            limit = box.width / 2 - 4;
            if (limit > box.height / 2 - 4) {
                limit = box.height / 2 - 4;
            }
        }
        var text = (_now == null) ? "--" : _now.format("%d");
        var font = Graphics.FONT_NUMBER_MILD;
        if (dc.getTextWidthInPixels(text, font) > limit * 2 - 4) {
            font = Graphics.FONT_SMALL;
        }
        dc.setColor(_fg, Graphics.COLOR_TRANSPARENT);
        dc.drawText(cx, cy, font, text, Graphics.TEXT_JUSTIFY_CENTER | Graphics.TEXT_JUSTIFY_VCENTER);
        if (_now == null) {
            return;
        }
        var w = dc.getTextWidthInPixels(text, font);
        var h = Graphics.getFontHeight(font);
        var dx = cx + w / 2 + 1;
        var dy = cy - h / 2 + 3;
        var ox = dx - cx;
        var oy = dy - cy;
        if (ox * ox + oy * oy < limit * limit) {
            dc.setColor(_fg, _fg);
            dc.drawCircle(dx, dy, 2);
        }
    }

    // AMOLED always-on rule, used only when the device asks for it. This MIP
    // face normally keeps the full chart. Here the time jumps between four
    // non-overlapping slots so no pixel stays lit for three minutes, and the
    // background stays black so the lit share stays small.
    function drawBurnIn(dc) {
        dc.setColor(Graphics.COLOR_WHITE, Graphics.COLOR_BLACK);
        dc.clear();
        dc.setPenWidth(1);
        var clock = System.getClockTime();
        var slot = clock.min % 4;
        var x = (slot % 2 == 0) ? 28 : 104;
        var y = (slot < 2) ? 82 : 112;
        var hour = clock.hour;
        if (!System.getDeviceSettings().is24Hour) {
            hour = hour % 12;
            if (hour == 0) {
                hour = 12;
            }
            hour = hour.format("%d");
        } else {
            hour = hour.format("%02d");
        }
        var text = hour + ":" + clock.min.format("%02d");
        dc.drawText(x, y, Graphics.FONT_NUMBER_MILD, text, Graphics.TEXT_JUSTIFY_LEFT);
        if (_has) {
            scaleTemps();
            var base = 156 + (slot % 3);
            for (var i = 0; i < _count - 1; i++) {
                var x0 = CHART_X + (i * CHART_W) / _count;
                var x1 = CHART_X + ((i + 1) * CHART_W) / _count;
                dc.drawLine(x0, base - spark(_temp[i]), x1, base - spark(_temp[i + 1]));
            }
        }
        drawSubscreen(dc);
    }

    function spark(temp) {
        var span = _hi - _lo;
        if (span < 1) {
            span = 1;
        }
        return ((temp - _lo) * 8) / span;
    }
}
