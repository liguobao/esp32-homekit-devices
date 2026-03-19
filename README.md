# ESP32 HomeKit

这是一个基于 Espressif `esp-homekit-sdk` 的 HomeKit 示例工程。
当前仓库内置 `outlet`、`light`、`dashboard` 和 `epaper` 四种设备类型，
其中 `outlet/light/dashboard` 默认面向 `ESP32-C3`，`epaper` 面向 `Waveshare ESP32-S3-ePaper-1.54 V2`。

英文版文档见 [README.en.md](README.en.md)。

## 前置依赖

- 已安装 `ESP-IDF 5.4.2`，默认路径是 `$HOME/.espressif/frameworks/esp-idf-v5.4.2`
- 已连接一块可用的开发板：
  `ESP32-C3`（`outlet/light/dashboard`）或 `Waveshare ESP32-S3-ePaper-1.54 V2`（`epaper`）
- 使用本地文件保存 Wi-Fi 凭据，不要直接改仓库默认配置

准备本地 Wi-Fi 配置：

```sh
cp sdkconfig.defaults.local.example sdkconfig.defaults.local
# 或者，Waveshare V2 墨水屏板：
cp sdkconfig.defaults.local.epaper-v2.example sdkconfig.defaults.local
```

说明：`build-device.sh`、`flash-device.sh` 和 `flash-device.ps1` 会在每次执行前删除 `sdkconfig` 与 `sdkconfig.old`，
并优先读取 `sdkconfig.defaults.local` 里的目标芯片设置。
如果本地文件没有指定目标芯片，`outlet/light/dashboard` 默认走 `sdkconfig.defaults` 里的 `esp32c3`，
`epaper` 默认走 `esp32s3`，确保不同设备类型能自动切到正确目标。

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

切到 `epaper`：

```sh
./scripts/build-device.sh epaper
./scripts/flash-device.sh epaper -p /dev/cu.usbmodemXXXX
./scripts/monitor.sh -p /dev/cu.usbmodemXXXX
```

## Waveshare ESP32-S3-ePaper-1.54 V2

`epaper` 设备类型把 Waveshare `V2` 官方 `ESP-IDF` 例程和配套板级组件里的主要逻辑迁了进来，
当前已经接入：

- `1.54` 寸 `200x200` 墨水屏驱动与刷新流程
- `SHTC3` 温湿度采集
- `PCF85063` RTC 读时
- 锂电池电压检测与 HomeKit 电池服务
- 音频录音、回放，以及 SD 音乐 / 固件内置音频回退播放
- `Micro SD` 挂载、状态快照写入，以及可选 CSV 日志
- `BOOT/PWR` 自定义按键逻辑
- `RTC` 定时唤醒与深度睡眠
- V2 板级供电控制与本地看板显示

当前支持 `V2` 版本，不支持 `V1`。
官方 V1/V2 例程不通用，这里迁移的是 `V2` 分支：

- `Example/ESP-IDF/V2/12_RTC_Sleep_Test`
- `Example/ESP-IDF/V2/03_I2C_SHTC3`
- `Example/ESP-IDF/V2/02_I2C_PCF85063`

额外说明：

- `epaper` 默认使用 `8MB Flash / 8MB PSRAM` 的 V2 资源配置，并切到 `partitions_hap_epaper.csv`
- 看板会显示温湿度、RTC 时间、电池、SD、音频状态和唤醒来源
- 默认按键映射：
  `BOOT` 单击录音，双击播放录音，长按 `3s` 重置 Wi-Fi，长按 `10s` 恢复出厂；
  `PWR` 单击优先播放 `/sdcard/music` 下的首个 `mp3/wav`，无 SD 或无音频时回退到固件内置音频；双击写入 SD 状态快照，长按进入深度睡眠

更详细的板级说明见 [devices/epaper/README.md](devices/epaper/README.md)。

## Windows (PowerShell)

如果在 Windows 上使用普通 `PowerShell`，先设置目标芯片，再执行脚本或纯 `idf.py` 命令。
`outlet/light/dashboard` 默认以 `ESP32-C3` 为目标；
如果你在用 `epaper`，请把下面命令里的 `esp32c3` 改成 `esp32s3`，
或者直接复制 `sdkconfig.defaults.local.epaper-v2.example`。

```powershell
Copy-Item sdkconfig.defaults.local.example sdkconfig.defaults.local

$idfPath = "$env:USERPROFILE\.espressif\frameworks\esp-idf-v5.4.2"
cmd /c """$idfPath\export.bat"" && idf.py set-target esp32c3"
```

使用 PowerShell 脚本烧录：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\flash-device.ps1 outlet -p COM5
powershell -ExecutionPolicy Bypass -File .\scripts\flash-device.ps1 epaper -p COM5
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
# `outlet/light/dashboard`
idf.py set-target esp32c3
# `epaper`
# idf.py set-target esp32s3
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

手动构建 `epaper`：

```sh
export IDF_PATH=$HOME/.espressif/frameworks/esp-idf-v5.4.2
. "$IDF_PATH/export.sh"
idf.py -DIDF_TARGET=esp32s3 -DHOMEKIT_DEVICE_TYPE=epaper reconfigure build
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
