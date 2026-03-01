# Devices

This directory contains different HomeKit device implementations and their shared helpers.

## Current Layout

- `common/`
  Shared hardware helpers, such as the current GPIO output wrapper
- `include/device.h`
  Shared device interface definition
- `device.c`
  Dispatcher for the active device type
- `<type>/`
  One directory per device type, such as `outlet/` and `light/`

## How to Add a New Device

1. Create `devices/<name>/` with `<name>_device.c` and `<name>_device.h`
2. Export a `const homekit_device_t *<name>_device_get(void)` following existing patterns
3. Fill in `name_prefix`, `cid`, `identify`, `add_services`, and `init_hardware`
4. Reuse `devices/common/gpio_output.c` when possible; add a new common helper only when needed
5. Add the new source file and private include directory in `devices/CMakeLists.txt`
6. Extend `devices/device.c` with the new selection branch
7. Extend the valid `HOMEKIT_DEVICE_TYPE` values in the top-level `CMakeLists.txt`
8. Add a `devices/<name>/README.md` and optionally `README.en.md`

## Current Selection Model

The project currently ships with `outlet` and `light`, selected by `HOMEKIT_DEVICE_TYPE` at build time.

Example:

```sh
HOMEKIT_DEVICE_TYPE=outlet idf.py reconfigure build
HOMEKIT_DEVICE_TYPE=light idf.py reconfigure build
```
