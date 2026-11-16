# Weatherline

Weatherline is a watchface for Pebble Time 2 (emery) and Pebble Round 2 (gabbro). It draws an hour-by-hour forecast with the time, the place, today's steps, the battery, and the Bluetooth state.

The chart is a temperature line. Rain and snow are bars along the bottom. Cloud cover sits across the top, with a sun where the sky is clear. Night hours are shaded, and each day is labeled with its high and low.

## Build

From this directory, run:

```
pebble build
```

The SDK is Pebble 4.x. `package.json` keeps `sdkVersion` at `"3"`, which is what the tool expects.

## Install

Install on an emulator with:

```
pebble install --emulator emery
pebble install --emulator gabbro
```

Install on a phone at a known address with:

```
pebble install --phone <ip>
```

## Settings

Open the config page from the Pebble phone app. There are three settings.

- Days shown are 1, 2, 3, or 5. The default is 2.
- The temperature unit is °F or °C. The default is °F.
- Refresh is 30, 45, or 60 minutes. The default is 60.

Changing the day count or the unit redraws the forecast already stored on the phone. It does not download again.

The phone tries a network location first, then GPS, and keeps the last fix. If a refresh fails, the watch keeps showing that forecast. A `!` after the temperature means the forecast is more than two hours old.

## Data

Forecasts come from [Open-Meteo](https://open-meteo.com/) and are licensed under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/). Place names come from the BigDataCloud reverse geocoder. Neither service needs an API key.
