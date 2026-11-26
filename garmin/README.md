# Weatherline for Garmin

Connect IQ watch face for the Instinct 3 Solar. The 45mm and the 50mm share one device id, `instinct3solar45mm`. The face reads current conditions and the hourly forecast from the paired phone through `Toybox.Weather`. It does not use an API key or make web requests.

The screen is 176×176, 1-bit, and always on, with a round subscreen at the upper right. Current temperature is drawn in that subscreen. The hourly chart sits in the full-width band underneath it: temperature line, cloud bank, sun on a clear daytime hour, and a precipitation lane. Rain grows up from the middle of the lane and snow grows down. The bar height is the chance of precipitation. Time and date sit at the bottom, with steps, battery, and a phone mark when the phone is disconnected.

## Setup

Install the [Connect IQ SDK Manager](https://developer.garmin.com/connect-iq/sdk/) and, from it, the SDK and the Instinct 3 Solar 45mm / 50mm device. This folder does not include the SDK or the device files.

Generate a developer key locally and do not commit it:

```sh
openssl genrsa -out developer_key.pem 4096
openssl pkcs8 -topk8 -inform PEM -outform DER -in developer_key.pem -out developer_key.der -nocrypt
```

## Build

From this directory, with `monkeyc` on your `PATH` (it lives in the SDK `bin` directory):

```sh
monkeyc -f monkey.jungle -o bin/weatherline.prg -y developer_key.der -d instinct3solar45mm -w
```

Run it in the simulator:

```sh
monkeydo bin/weatherline.prg instinct3solar45mm
```

In the simulator, **Settings → Set Weather** fills current conditions and the hourly forecast. **Settings → Display Mode → Always-Active** shows the low-power drawing: a 1px temperature line, a tick instead of the now marker, and a sun with no rays. This MIP reports no burn-in requirement. If a device does require burn-in protection, the time steps between four positions while the chart is a 1px sparkline.

## Sideload

Plug the watch in over USB and copy `bin/weatherline.prg` to `GARMIN/Apps/`. Eject the watch. The face appears with the other watch faces. Later builds have to be signed with the same key.

## Settings

The Connect IQ app settings are:

- Hours on the chart: 12 or 24. The chart draws as many hours as the phone actually provides.
- Temperature unit: follow the watch, Fahrenheit, or Celsius.
- Light background.
- Vibrate if the phone disconnects.

## What the forecast does not include

Hourly weather from the phone provides temperature, condition, precipitation chance, and cloud cover. The chart uses those. Feels-like temperature is only on the current observation, and the hourly forecast has no millimeter amount and no previous-day series, so the line is the actual temperature and the bars are the chance of precipitation. Day and night use sunrise and sunset when the watch has a position, and 07:00–17:00 otherwise.
