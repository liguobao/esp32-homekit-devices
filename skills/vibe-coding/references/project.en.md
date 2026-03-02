# Project Reference

## Basics

- Target chip: `ESP32-C3`
- Toolchain: `ESP-IDF 5.4.2`
- HomeKit stack: vendored `third_party/esp-homekit-sdk`
- Default device type: `outlet`
- Build switch: `HOMEKIT_DEVICE_TYPE=outlet|light`

## Layout

- `main/app_main.c`: shared Wi-Fi and HomeKit bootstrap
- `devices/device.c`: active device selection
- `devices/<type>/`: device-specific implementation
- `devices/common/`: shared hardware helpers
- `scripts/`: local build, flash, and monitor wrappers
- `docs/`: core project docs

## Constraints

- Do not commit real Wi-Fi credentials; keep them in ignored local files only.
- Keep shared Wi-Fi / HomeKit setup in `main/app_main.c`; do not duplicate it per device.
- Put device-specific behavior in `devices/<type>/`.
- Keep `HOMEKIT_DEVICE_TYPE` as the primary build-time selector.
- Current shared GPIO output is `GPIO2` with `active-low`; update docs if that changes.
- Root `README.md` should stay minimal; move detailed docs into `docs/`, `scripts/README.md`, and `devices/README.md`.
- User-facing docs should default to Chinese, with English versions in separate `.en.md` files.

## Common Changes

- Adding a device:
  Read `devices/README.md`, then add `devices/<name>/`, update `devices/CMakeLists.txt`, `devices/device.c`, and the top-level `CMakeLists.txt`.

- Updating docs:
  Keep Chinese content in the main `.md` file and put English content in a separate `.en.md` file.

- Running common tasks:
  Prefer `scripts/build-device.sh`, `scripts/flash-device.sh`, `scripts/flash-device.ps1`, and `scripts/monitor.sh`.
