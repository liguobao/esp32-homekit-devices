# Core Notes

## Current Defaults

- Target chip: `ESP32-C3`
- Default device type: `Outlet`
- Default accessory name prefix: `Home Outlet`
- Default output GPIO: `GPIO2`
- Output logic: `active-low`
- Wi-Fi configuration: hardcoded through a local override file
- HomeKit setup code: `111-22-333`

`active-low` means the output GPIO is driven low when the accessory is on.

## Project Layout

- `main/app_main.c`
  Shared Wi-Fi and HomeKit bootstrap
- `devices/`
  Device implementations and shared helpers
- `scripts/`
  Local build, flash, and monitor scripts
- `skills/`
  Repo-local AI workflow skills

## Core Dependencies

- `ESP-IDF 5.4.2`
  Provides chip support, the build system, drivers, Wi-Fi, NVS, and logging
- `esp-homekit-sdk`
  Vendored under `third_party/esp-homekit-sdk` as the HomeKit protocol stack
- Espressif Component Registry packages
  Locked in `dependencies.lock`, including `mdns`, `libsodium`, `json_parser`, `json_generator`, and `jsmn`

## Add to Apple Home

1. Wait until the device joins Wi-Fi.
2. Make sure the iPhone or iPad is on the same LAN.
3. Open the Home app and choose Add Accessory.
4. Scan the QR code, or choose the manual flow.
5. Select the nearby `Home Outlet-XXXX`.
6. Enter `11122333`.

QR link:

[HomeKit Setup QR Code](https://espressif.github.io/esp-homekit-sdk/qrcode.html?data=X-HM://00718C331C3HK)

## Wiring

- Connect one side of the load to `3.3V`
- Connect the other side to `GPIO2`
- The default output is active-low

For a plain LED, use a series resistor.
For modules or larger loads, use a transistor or MOSFET driver stage.

## Credits and Upstream

- `ESP-IDF`: `https://github.com/espressif/esp-idf`
- `esp-homekit-sdk`: `https://github.com/espressif/esp-homekit-sdk`
- Espressif Component Registry: `https://components.espressif.com/`

Thanks to Espressif and the maintainers of the upstream open-source projects.
