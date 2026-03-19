# ePaper 设备

这是一个面向 `Waveshare ESP32-S3-ePaper-1.54 V2` 的 HomeKit 墨水屏设备类型，
把官方 `V2` 板级逻辑整体迁进了当前工程。

运行时行为：

- 暴露 1 个 HomeKit 温度传感器服务
- 暴露 1 个 HomeKit 湿度传感器服务
- 暴露 1 个 HomeKit 电池服务
- 读取板载 `SHTC3` 温湿度传感器
- 读取板载 `PCF85063` RTC 时间
- 检测锂电池电压并换算电量百分比
- 初始化音频编解码链路，支持录音、录音回放，以及 SD 音乐 / 固件内置音频回退播放
- 挂载 `Micro SD`，支持状态快照和可选 CSV 数据日志
- 使用板载 `BOOT/PWR` 按键处理录音、回放、SD 快照、网络重置和休眠
- 支持 `RTC` 定时唤醒与深度睡眠
- 在板载 `1.54` 寸 `200x200` 墨水屏上显示温度、湿度、时间、电池、SD、音频状态和唤醒来源

迁移来源：

- `Example/ESP-IDF/V2/12_RTC_Sleep_Test`
  提供板级供电时序、1.54 寸墨水屏初始化与刷新流程
- `Example/ESP-IDF/V2/03_I2C_SHTC3`
  提供 `SHTC3` 初始化、CRC 校验与温湿度换算逻辑
- `Example/ESP-IDF/V2/02_I2C_PCF85063`
  提供 `PCF85063` RTC 读时逻辑

此外复用了上游 `codec_board` 组件里的音频与 `Micro SD` 底层适配，
用于接入 `ES8311`、扬声器、麦克风和 SD 卡槽相关逻辑。

## 支持范围

- 仅支持 `V2` 硬件
- `V1` 与 `V2` 的芯片、内存和官方例程均不兼容，不能混用
- 默认目标芯片是 `esp32s3`
- 默认使用 `8MB Flash / 8MB PSRAM`
- 默认使用 `partitions_hap_epaper.csv` 分区表，给 `epaper` 固件预留更大的 OTA 空间

## 默认按键行为

- `BOOT` 单击：录音
- `BOOT` 双击：播放最近一次录音
- `BOOT` 长按释放：
  超过 `3s` 重置 Wi-Fi，超过 `10s` 恢复出厂
- `PWR` 单击：优先播放 `/sdcard/music` 下的首个 `mp3/wav`，无 SD 或无可用音频时回退到固件内置音频
- `PWR` 双击：写入 `/sdcard/homekit-epaper-status.txt`
- `PWR` 长按释放：进入深度睡眠，并按配置的 `RTC` 周期自动唤醒

## 板级默认配置

- 墨水屏 SPI
  `DC=10`、`CS=11`、`SCLK=12`、`MOSI=13`、`RST=9`、`BUSY=8`
- 供电控制
  `PWR=6`、`AUDIO_PWR=42`、`VBAT_PWR=17`
- 按键与 RTC 中断
  `BOOT=0`、`PWR_KEY=21`、`RTC_INT=5`
- I2C
  `SDA=47`、`SCL=48`
- 设备地址
  `PCF85063=0x51`、`SHTC3=0x70`
- 温度补偿
  默认 `-4.0 C`，对应 `CONFIG_HOMEKIT_EPAPER_TEMP_OFFSET_DECICELSIUS=-40`

这些默认值可以在 `menuconfig -> Example Configuration -> ePaper Device Configuration` 里调整。

常用可调项：

- `HOMEKIT_EPAPER_REFRESH_INTERVAL_SEC`
  看板轮询和刷新周期
- `HOMEKIT_EPAPER_AUDIO_BUFFER_KB`
  录音缓冲区大小，默认走 PSRAM
- `HOMEKIT_EPAPER_SLEEP_WAKE_INTERVAL_SEC`
  深度睡眠后 RTC 自动唤醒周期
- `HOMEKIT_EPAPER_LOW_BATTERY_LEVEL`
  HomeKit 低电量阈值
- `HOMEKIT_EPAPER_ENABLE_SD_LOGGING`
  是否向 `/sdcard/homekit-epaper-log.csv` 追加日志

## 快速开始

```sh
cp sdkconfig.defaults.local.epaper-v2.example sdkconfig.defaults.local
# 编辑 Wi-Fi 凭据
# 如需 Micro SD，请先格式化为 FAT32

./scripts/build-device.sh epaper
./scripts/flash-device.sh epaper -p /dev/cu.usbmodemXXXX
./scripts/monitor.sh -p /dev/cu.usbmodemXXXX
```

如果不用脚本，直接执行：

```sh
rm -f sdkconfig sdkconfig.old
idf.py -DIDF_TARGET=esp32s3 -DHOMEKIT_DEVICE_TYPE=epaper reconfigure build
idf.py -p /dev/cu.usbmodemXXXX flash
idf.py -p /dev/cu.usbmodemXXXX monitor
```
