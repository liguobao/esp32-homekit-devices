# ESP32 HomeKit

这是一个基于 Espressif `esp-homekit-sdk` 的 `ESP32-C3` HomeKit 示例工程。
当前仓库内置 `outlet` 和 `light` 两种设备类型。

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
确保 `sdkconfig.defaults.local` 的最新改动会重新生成到新的配置文件里。

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
