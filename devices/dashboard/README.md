# Dashboard 设备

这是一个三路灯的 HomeKit 面板设备类型。

运行时行为：

- 暴露 3 个独立的 HomeKit 灯服务
- 左侧 `ST7789` 长条屏显示当前时间和天气
- 右侧 `NV3007` 长条屏显示当前 IP 和 3 路灯状态

推荐接线（经典 `ESP32`）：

- 共享 SPI
  `GPIO18` -> 两块屏的 `SCL/CLK/SCK`
  `GPIO23` -> 两块屏的 `SDA/MOSI/DIN`
- 左侧 `ST7789`（`76x284`）
  `GPIO16` -> `CS`
  `GPIO17` -> `DC/RS/A0`
  `GPIO21` -> `RST/RES`
  `GPIO22` -> `BL/LED`
- 右侧 `NV3007`（`142x428`）
  `GPIO25` -> `CS`
  `GPIO26` -> `DC/RS/A0`
  `GPIO27` -> `RST/RES`
  `GPIO32` -> `BL/LED`
- 三路灯输出
  `GPIO13` -> Light 1
  `GPIO14` -> Light 2
  `GPIO33` -> Light 3
- 电源
  两块屏的 `VCC` 接 `3V3`
  两块屏的 `GND` 接 `GND`

如果背光不需要软件控制，可以把 `BL/LED` 直接接 `3V3`，并在 `menuconfig` 里把对应 `BL` 改成 `-1`。

需要在 `menuconfig -> Example Configuration -> Dashboard Device Configuration` 里同步填入这些 GPIO。

构建示例：

```sh
HOMEKIT_DEVICE_TYPE=dashboard idf.py reconfigure build
```
