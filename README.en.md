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

Note: `build-device.sh`, `flash-device.sh`, and `flash-device.ps1` delete `sdkconfig` and `sdkconfig.old`
before each run so the latest `sdkconfig.defaults.local` changes are always rebuilt
into a fresh config.

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

## Windows (PowerShell)

If you are using regular `PowerShell` on Windows, set the target first, then use either the script wrapper or plain `idf.py`.
The default examples still target `ESP32-C3`; if you are using a classic `ESP32` board, replace `esp32c3` below with `esp32`.

```powershell
Copy-Item sdkconfig.defaults.local.example sdkconfig.defaults.local

$idfPath = "$env:USERPROFILE\.espressif\frameworks\esp-idf-v5.4.2"
cmd /c """$idfPath\export.bat"" && idf.py set-target esp32c3"
```

Flash with the PowerShell wrapper:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\flash-device.ps1 outlet -p COM5
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
