# ESP32 HomeKit

This is an `ESP32-C3` HomeKit demo project based on Espressif `esp-homekit-sdk`.
The repository currently ships with two device variants: `outlet` and `light`.

## Prerequisites

- Install `ESP-IDF 5.4.2`, typically at `$HOME/.espressif/frameworks/esp-idf-v5.4.2`
- Connect a working `ESP32-C3` development board
- Keep Wi-Fi credentials in the local override file instead of tracked defaults

Prepare local Wi-Fi config:

```sh
cp sdkconfig.defaults.local.example sdkconfig.defaults.local
```

## Quick Scripts

Most common commands:

```sh
./scripts/build-outlet.sh
./scripts/flash-outlet.sh -p /dev/cu.usbmodemXXXX
./scripts/monitor.sh -p /dev/cu.usbmodemXXXX
```

Switch to `light`:

```sh
./scripts/build-light.sh
./scripts/flash-light.sh -p /dev/cu.usbmodemXXXX
./scripts/monitor.sh -p /dev/cu.usbmodemXXXX
```

## Manual Build, Flash, and Monitor

Run this once for the first build, or after changing the target chip:

```sh
export IDF_PATH=$HOME/.espressif/frameworks/esp-idf-v5.4.2
. "$IDF_PATH/export.sh"
idf.py set-target esp32c3
```

Build `outlet` manually:

```sh
export IDF_PATH=$HOME/.espressif/frameworks/esp-idf-v5.4.2
. "$IDF_PATH/export.sh"
HOMEKIT_DEVICE_TYPE=outlet idf.py reconfigure build
```

Build `light` manually:

```sh
export IDF_PATH=$HOME/.espressif/frameworks/esp-idf-v5.4.2
. "$IDF_PATH/export.sh"
HOMEKIT_DEVICE_TYPE=light idf.py reconfigure build
```

Flash and monitor manually:

```sh
export IDF_PATH=$HOME/.espressif/frameworks/esp-idf-v5.4.2
. "$IDF_PATH/export.sh"
idf.py -p /dev/cu.usbmodemXXXX flash
idf.py -p /dev/cu.usbmodemXXXX monitor
```

## More Docs

- [docs/README.en.md](docs/README.en.md)
- [scripts/README.en.md](scripts/README.en.md)
- [devices/README.en.md](devices/README.en.md)
- [skills/README.en.md](skills/README.en.md)
