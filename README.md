# AnalogSense.asi

Bringing support for analog keyboard input to games.

[![IMAGE ALT TEXT HERE](https://img.youtube.com/vi/xIVNbDQNNW4/0.jpg)](https://www.youtube.com/watch?v=xIVNbDQNNW4)

## Supported Games

- Cyberpunk 2077
- Grand Theft Auto V

## Prerequisites

This plugin requires the [Wooting Analog SDK](https://github.com/WootingKb/wooting-analog-sdk/releases) to be installed. However, that doesn't mean it can only be used with Wooting keyboards, as there are [SDK plugins for other analog keyboards](https://github.com/AnalogSense/universal-analog-plugin).

## Response Curve

By default, your input is mapped with a linear curve. However, you can add additional points to `%programdata%\AnalogSense\curve_points.json`. You can use [this online tool](https://analogsense.org/Response-Curve-Configurator/) to generate this file for you — it also visualizes & simulates the response curve.
