# ESP32 HomeKit

This is a HomeKit demo project based on Espressif `esp-homekit-sdk`.
The repository currently ships with four device variants: `outlet`, `light`,
`dashboard`, and `epaper`.
`outlet/light/dashboard` target `ESP32-C3` by default, while `epaper` targets
the `Waveshare ESP32-S3-ePaper-1.54 V2`.

## Prerequisites

- Install `ESP-IDF 5.4.2`, typically at `$HOME/.espressif/frameworks/esp-idf-v5.4.2`
- Connect a supported board:
  `ESP32-C3` for `outlet/light/dashboard`, or `Waveshare ESP32-S3-ePaper-1.54 V2` for `epaper`
- Keep Wi-Fi credentials in the local override file instead of tracked defaults

Prepare local Wi-Fi config:

```sh
cp sdkconfig.defaults.local.example sdkconfig.defaults.local
# Or, for the Waveshare V2 ePaper board:
cp sdkconfig.defaults.local.epaper-v2.example sdkconfig.defaults.local
```

Note: `build-device.sh`, `flash-device.sh`, and `flash-device.ps1` delete `sdkconfig` and `sdkconfig.old`
before each run, and prefer the target set in `sdkconfig.defaults.local`.
If no local target is set, `outlet/light/dashboard` fall back to `esp32c3` from
`sdkconfig.defaults`, while `epaper` falls back to `esp32s3`.

## Quick Scripts

Most common commands:

```sh
./scripts/build-device.sh outlet
./scripts/flash-device.sh outlet -p /dev/cu.usbmodemXXXX
./scripts/monitor.sh -p /dev/cu.usbmodemXXXX
```

Switch to `light`:

```sh
./scripts/build-device.sh light
./scripts/flash-device.sh light -p /dev/cu.usbmodemXXXX
./scripts/monitor.sh -p /dev/cu.usbmodemXXXX
```

Switch to `dashboard`:

```sh
./scripts/build-device.sh dashboard
./scripts/flash-device.sh dashboard -p /dev/cu.usbmodemXXXX
./scripts/monitor.sh -p /dev/cu.usbmodemXXXX
```

Switch to `epaper`:

```sh
./scripts/build-device.sh epaper
./scripts/flash-device.sh epaper -p /dev/cu.usbmodemXXXX
./scripts/monitor.sh -p /dev/cu.usbmodemXXXX
```

## Waveshare ESP32-S3-ePaper-1.54 V2

The `epaper` device type ports the main board logic from Waveshare's official
`ESP-IDF` V2 examples and companion board components:

- `1.54-inch` `200x200` ePaper driver and refresh flow
- `SHTC3` temperature and humidity sampling
- `PCF85063` RTC reads
- battery voltage reporting plus a HomeKit battery service
- audio record, playback, and demo PCM playback
- `Micro SD` mounting, status snapshots, and optional CSV logging
- custom `BOOT/PWR` button handling
- `RTC` timed wake and deep-sleep flow
- V2 board power sequencing plus the local dashboard UI

Only `V2` hardware is supported here. `V1` and `V2` use different official examples.

- `Example/ESP-IDF/V2/12_RTC_Sleep_Test`
- `Example/ESP-IDF/V2/03_I2C_SHTC3`
- `Example/ESP-IDF/V2/02_I2C_PCF85063`

Additional notes:

- `epaper` uses the V2 `8MB Flash / 8MB PSRAM` defaults and the `partitions_hap_epaper.csv` layout
- The dashboard now shows temperature, humidity, RTC time, battery, SD, audio status, and wake reason
- Default button mapping:
  `BOOT` single-click records audio, double-click plays the recording, hold `3s` resets Wi-Fi, hold `10s` resets to factory settings;
  `PWR` single-click plays the demo clip, double-click writes an SD status snapshot, and long press enters deep sleep

See [devices/epaper/README.en.md](devices/epaper/README.en.md) for board details.

## Windows (PowerShell)

If you are using regular `PowerShell` on Windows, set the target first, then use either the script wrapper or plain `idf.py`.
`outlet/light/dashboard` still default to `ESP32-C3`.
If you are using `epaper`, replace `esp32c3` below with `esp32s3`, or copy
`sdkconfig.defaults.local.epaper-v2.example` first.

```powershell
Copy-Item sdkconfig.defaults.local.example sdkconfig.defaults.local

$idfPath = "$env:USERPROFILE\.espressif\frameworks\esp-idf-v5.4.2"
cmd /c """$idfPath\export.bat"" && idf.py set-target esp32c3"
```

Flash with the PowerShell wrapper:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\flash-device.ps1 outlet -p COM5
powershell -ExecutionPolicy Bypass -File .\scripts\flash-device.ps1 epaper -p COM5
cmd /c """$idfPath\export.bat"" && idf.py -p COM5 monitor"
```

If you do not want to use helper scripts, run `idf.py` directly:

```powershell
Remove-Item .\sdkconfig, .\sdkconfig.old -Force -ErrorAction SilentlyContinue
cmd /c """$idfPath\export.bat"" && idf.py -DHOMEKIT_DEVICE_TYPE=outlet -p COM5 reconfigure flash"
cmd /c """$idfPath\export.bat"" && idf.py -p COM5 monitor"
```

If you only want to rebuild without flashing:

```powershell
Remove-Item .\sdkconfig, .\sdkconfig.old -Force -ErrorAction SilentlyContinue
cmd /c """$idfPath\export.bat"" && idf.py -DHOMEKIT_DEVICE_TYPE=outlet reconfigure build"
```

## ST7789 Display

This version now includes optional `ST7789` SPI display support. It is disabled
by default and does not change the existing `outlet/light` behavior unless you
turn it on.

When enabled, the screen shows the accessory name, model, setup code, and the
current relay on/off state.

Configure it with:

```sh
idf.py menuconfig
```

Then open `Example Configuration -> ST7789 Display Configuration` and set at
least these GPIOs:

- `SPI clock GPIO`
- `SPI MOSI GPIO`
- `SPI CS GPIO`
- `SPI DC GPIO`

Useful optional settings:

- `Reset GPIO` and `Backlight GPIO`
- Resolution (default `240x240`)
- `X/Y offset`
- `Swap X/Y`, `Mirror X/Y`
- `Invert colors`
- `Use BGR color order`

Recommended wiring (classic `ESP32`, using the `ST7789` strip by itself):

- `GPIO18` -> `SCL/CLK/SCK`
- `GPIO23` -> `SDA/MOSI/DIN`
- `GPIO16` -> `CS`
- `GPIO17` -> `DC/RS/A0`
- `GPIO21` -> `RST/RES`
- `GPIO22` -> `BL/LED` (or wire it directly to `3V3` and set `Backlight GPIO` to `-1`)
- `3V3` -> `VCC`
- `GND` -> `GND`

Set the panel resolution to `76x284`, then fine-tune `X/Y offset`, `Swap X/Y`, and
`Mirror X/Y` to match your module orientation.

## Manual Build, Flash, and Monitor

Run this once for the first build, or after changing the target chip:

```sh
export IDF_PATH=$HOME/.espressif/frameworks/esp-idf-v5.4.2
. "$IDF_PATH/export.sh"
# `outlet/light/dashboard`
idf.py set-target esp32c3
# `epaper`
# idf.py set-target esp32s3
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

Build `dashboard` manually:

```sh
export IDF_PATH=$HOME/.espressif/frameworks/esp-idf-v5.4.2
. "$IDF_PATH/export.sh"
HOMEKIT_DEVICE_TYPE=dashboard idf.py reconfigure build
```

Build `epaper` manually:

```sh
export IDF_PATH=$HOME/.espressif/frameworks/esp-idf-v5.4.2
. "$IDF_PATH/export.sh"
idf.py -DIDF_TARGET=esp32s3 -DHOMEKIT_DEVICE_TYPE=epaper reconfigure build
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
