import Toybox.Application;
import Toybox.WatchUi;

// Weatherline for the Instinct 3 Solar. One watch face. The phone's Garmin
// weather cache is the only data source.
class WeatherlineApp extends Application.AppBase {

    function initialize() {
        AppBase.initialize();
    }

    function getInitialView() {
        return [new WeatherlineView(), new WeatherlineDelegate()];
    }

    function onSettingsChanged() {
        WatchUi.requestUpdate();
    }
}

class WeatherlineDelegate extends WatchUi.WatchFaceDelegate {

    function initialize() {
        WatchFaceDelegate.initialize();
    }
}
