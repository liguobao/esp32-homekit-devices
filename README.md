# ESP32 HomeKit

这是一个基于 Espressif `esp-homekit-sdk` 的 `ESP32-C3` HomeKit 示例工程。
当前仓库内置 `outlet`、`light` 和 `dashboard` 三种设备类型。

英文版文档见 [README.en.md](README.en.md)。

## 前置依赖

- 已安装 `ESP-IDF 5.4.2`，默认路径是 `$HOME/.espressif/frameworks/esp-idf-v5.4.2`
- 已连接一块可用的 `ESP32-C3` 开发板
- 使用本地文件保存 Wi-Fi 凭据，不要直接改仓库默认配置

准备本地 Wi-Fi 配置：

```sh
cp sdkconfig.defaults.local.example sdkconfig.defaults.local
```

说明：`build-device.sh`、`flash-device.sh` 和 `flash-device.ps1` 会在每次执行前删除 `sdkconfig` 与 `sdkconfig.old`，
并优先保留当前 `sdkconfig` 里的目标芯片设置，确保 `sdkconfig.defaults.local` 的最新改动会重新生成到新的配置文件里。

## 快捷脚本

最常用的脚本：

```sh
./scripts/build-device.sh outlet
./scripts/flash-device.sh outlet -p /dev/cu.usbmodemXXXX
./scripts/monitor.sh -p /dev/cu.usbmodemXXXX
```

切到 `light`：

```sh
./scripts/build-device.sh light
./scripts/flash-device.sh light -p /dev/cu.usbmodemXXXX
./scripts/monitor.sh -p /dev/cu.usbmodemXXXX
```

切到 `dashboard`：

```sh
./scripts/build-device.sh dashboard
./scripts/flash-device.sh dashboard -p /dev/cu.usbmodemXXXX
./scripts/monitor.sh -p /dev/cu.usbmodemXXXX
```

## Windows (PowerShell)

如果在 Windows 上使用普通 `PowerShell`，先设置目标芯片，再执行脚本或纯 `idf.py` 命令。
默认示例仍以 `ESP32-C3` 为目标；如果使用经典 `ESP32` 开发板，请把下面命令里的 `esp32c3` 改成 `esp32`。

```powershell
Copy-Item sdkconfig.defaults.local.example sdkconfig.defaults.local

$idfPath = "$env:USERPROFILE\.espressif\frameworks\esp-idf-v5.4.2"
cmd /c """$idfPath\export.bat"" && idf.py set-target esp32c3"
```

使用 PowerShell 脚本烧录：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\flash-device.ps1 outlet -p COM5
cmd /c """$idfPath\export.bat"" && idf.py -p COM5 monitor"
```

如果不用脚本，直接执行 `idf.py`：

```powershell
Remove-Item .\sdkconfig, .\sdkconfig.old -Force -ErrorAction SilentlyContinue
cmd /c """$idfPath\export.bat"" && idf.py -DHOMEKIT_DEVICE_TYPE=outlet -p COM5 reconfigure flash"
cmd /c """$idfPath\export.bat"" && idf.py -p COM5 monitor"
```

如果只想重新编译、不烧录：

```powershell
Remove-Item .\sdkconfig, .\sdkconfig.old -Force -ErrorAction SilentlyContinue
cmd /c """$idfPath\export.bat"" && idf.py -DHOMEKIT_DEVICE_TYPE=outlet reconfigure build"
```

## ST7789 显示屏

这个版本增加了可选的 `ST7789` SPI 显示支持，默认关闭，不影响现有 `outlet/light` 行为。
启用后会在屏幕上显示配件名、型号、配对码，以及当前继电器的开关状态。

配置方式：

```sh
idf.py menuconfig
```

进入 `Example Configuration -> ST7789 Display Configuration`，至少设置这些 GPIO：

- `SPI clock GPIO`
- `SPI MOSI GPIO`
- `SPI CS GPIO`
- `SPI DC GPIO`

常见可选项：

- `Reset GPIO` 和 `Backlight GPIO`
- 分辨率（默认 `240x240`）
- `X/Y offset`
- `Swap X/Y`、`Mirror X/Y`
- `Invert colors`
- `Use BGR color order`

推荐接线（经典 `ESP32`，单独使用这块 `ST7789` 长条屏）：

- `GPIO18` -> `SCL/CLK/SCK`
- `GPIO23` -> `SDA/MOSI/DIN`
- `GPIO16` -> `CS`
- `GPIO17` -> `DC/RS/A0`
- `GPIO21` -> `RST/RES`
- `GPIO22` -> `BL/LED`（如果背光常亮，也可以直接接 `3V3`，并把 `Backlight GPIO` 设为 `-1`）
- `3V3` -> `VCC`
- `GND` -> `GND`

建议把分辨率设置为 `76x284`，再根据模组方向微调 `X/Y offset`、`Swap X/Y` 和 `Mirror X/Y`。

## 纯手动命令

第一次构建或切换过目标芯片时：

```sh
export IDF_PATH=$HOME/.espressif/frameworks/esp-idf-v5.4.2
. "$IDF_PATH/export.sh"
idf.py set-target esp32c3
```

手动构建 `outlet`：

```sh
export IDF_PATH=$HOME/.espressif/frameworks/esp-idf-v5.4.2
. "$IDF_PATH/export.sh"
HOMEKIT_DEVICE_TYPE=outlet idf.py reconfigure build
```

手动构建 `light`：

```sh
export IDF_PATH=$HOME/.espressif/frameworks/esp-idf-v5.4.2
. "$IDF_PATH/export.sh"
HOMEKIT_DEVICE_TYPE=light idf.py reconfigure build
```

手动构建 `dashboard`：

```sh
export IDF_PATH=$HOME/.espressif/frameworks/esp-idf-v5.4.2
. "$IDF_PATH/export.sh"
HOMEKIT_DEVICE_TYPE=dashboard idf.py reconfigure build
```

手动烧录与监控：

```sh
export IDF_PATH=$HOME/.espressif/frameworks/esp-idf-v5.4.2
. "$IDF_PATH/export.sh"
idf.py -p /dev/cu.usbmodemXXXX flash
idf.py -p /dev/cu.usbmodemXXXX monitor
```

## 更多文档

- [docs/README.md](docs/README.md)
- [scripts/README.md](scripts/README.md)
- [devices/README.md](devices/README.md)
- [skills/README.md](skills/README.md)
