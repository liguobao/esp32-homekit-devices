# Outlet Device

This directory contains the default `Outlet` device implementation.

## Main Files

- `outlet_device.c`
  Creates the `Outlet` service, write callback, and device metadata
- `outlet_device.h`
  Exports `outlet_device_get()`

## Current Behavior

- HomeKit accessory type: `Outlet`
- Display name prefix: `Home Outlet`
- HomeKit service name: `Outlet`
- Main control: `On/Off`

## Runtime Behavior

- When `On` is written, it calls the shared `gpio_output_set_on()`
- Shared output control is implemented in `devices/common/gpio_output.c`
- The default output GPIO is `GPIO2`
- The default logic is `active-low`

## Build Selection

- Select this implementation with `HOMEKIT_DEVICE_TYPE=outlet`
- If no device type is specified, the project defaults to `outlet`
