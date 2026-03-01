# Light Device

This directory contains the `Light` implementation, exposed as a `Lightbulb` accessory in Apple Home.

## Main Files

- `light_device.c`
  Creates the `Lightbulb` service, write callback, and device metadata
- `light_device.h`
  Exports `light_device_get()`

## Current Behavior

- HomeKit accessory type: `Lightbulb`
- Display name prefix: `Home Light`
- HomeKit service name: `Light`
- Exposed controls: `On/Off`, `Brightness`, `Hue`, and `Saturation`

## Runtime Behavior

- `On/Off` calls the shared `gpio_output_set_on()`
- `Brightness`, `Hue`, and `Saturation` currently update in-memory state and HomeKit characteristics
- These values are not yet mapped to real PWM or multi-channel hardware output
- The current shared hardware output still comes from `devices/common/gpio_output.c`

## Build Selection

- Select this implementation with `HOMEKIT_DEVICE_TYPE=light`
- Typical command: `HOMEKIT_DEVICE_TYPE=light idf.py reconfigure build`
