# Weatherline

Weatherline is a watchface for Pebble Time 2 (emery) and Pebble Round 2 (gabbro). It draws an hour-by-hour forecast with the time, the place, today's steps, the battery, and the Bluetooth state.

The chart is a temperature line, with an orange marker for now. Rain and snow are bars along the bottom. Cloud cover is a band across the top, with a sun where the sky is clear. Night hours are shaded. Each day is labeled with its high and low, and with its name when more than one day is shown.

| Time 2 | Round 2 |
| --- | --- |
| ![Time 2, two days](screenshots/emery-2-day.png) | ![Round 2, two days](screenshots/gabbro-2-day.png) |
| ![Time 2, five days](screenshots/emery-5-day.png) | ![Round 2, five days](screenshots/gabbro-5-day.png) |

The top row shows today's steps and the battery. A red Bluetooth sign appears when the phone is disconnected.

## Build

From this directory, run:

```
pebble build
```

The SDK is Pebble 4.x. `package.json` keeps `sdkVersion` at `"3"`, which is what the tool expects.

## Test

The phone-side code has tests that run on Node 18 or later:

```
node --test tests/*.cjs
```

The watch layout lives in `src/pkjs/frame.js`. After changing it, regenerate the C header with `node scripts/emit-layout.js`.

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
- Refresh is every 30 minutes, 45 minutes, or 1 hour. The default is 1 hour.

Changing the day count or the unit redraws the forecast already stored on the phone. It does not download again.

The phone tries a network location first, then GPS, and keeps the last fix. The watch also asks the phone for a refresh every half hour and when Bluetooth reconnects, so the forecast stays current even if the phone pauses the app in the background. If a refresh fails, the watch keeps showing the last forecast. The temperature turns gray when the forecast is more than two hours old.

## Data

Forecasts come from [Open-Meteo](https://open-meteo.com/) and are licensed under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/). Place names come from the BigDataCloud reverse geocoder. Neither service needs an API key.
