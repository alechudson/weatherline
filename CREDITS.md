# Credits

Weatherline is original code by Alec Hudson except as noted below.
It is not a fork of any other Pebble project.

## Inspiration

The hour-by-hour forecast chart follows the idea of the **Weathergraph**
watchface. Layout, drawing, and the phone-side forecast code here are a
separate implementation. No code or assets were taken from that app.

- https://apps.repebble.com/weathergraph-weather-forecast-hourly-and-beautiful-with-step-counter-health-and-heartrate-hrm_56aeaa675fc3f1c4c700005b

## Weather

Forecast temperature, rain, showers, snowfall, cloud cover, daylight, and wind
come from [Open-Meteo](https://open-meteo.com/) and are licensed under
[CC BY 4.0](https://creativecommons.org/licenses/by/4.0/). Open-Meteo asks for
a link next to displayed data. A Pebble face cannot show a URL, so the
settings page says "Weather data by Open-Meteo.com (CC BY 4.0)." The same
credit is in the README.

- https://open-meteo.com/
- https://open-meteo.com/en/licence
- https://creativecommons.org/licenses/by/4.0/

## Place names

Place names come from BigDataCloud's free client-side reverse geocoding API.
Their code is not included here. Calls are made from the phone, using the
phone's current location.

- https://www.bigdatacloud.com/
- https://www.bigdatacloud.com/free-api/free-reverse-geocode-to-city-api
- https://www.bigdatacloud.com/docs/article/fair-use-policy-for-free-client-side-reverse-geocoding-api

## Wind barbs

Feather values follow the World Meteorological Organization station-model wind
barb: a short feather is 5 knots, a long feather is 10, and a pennant is 50.
The drawings in this repository are original.

## Type

The time is set in Barlow Condensed SemiBold, copyright 2017 The Barlow
Project Authors. The font file is in `resources/fonts/` under the SIL Open
Font License 1.1 (`resources/fonts/OFL.txt`). The reserved font name is
Barlow. Other text uses Pebble system fonts, which are not included here.

- https://github.com/jpt/barlow
- https://scripts.sil.org/OFL

## Platform

Built with the [Rebble / Pebble SDK](https://developer.repebble.com). The SDK
is not vendored in this repository.
