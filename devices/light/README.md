# 灯设备

这个目录包含 `Light` 设备实现，用于把配件作为 `Lightbulb` 暴露给 Apple 家庭。
英文版说明见 [README.en.md](README.en.md)。

## 主要文件

- `light_device.c`
  `Lightbulb` 服务创建、写回调、设备元信息
- `light_device.h`
  对外导出 `light_device_get()`

## 当前行为

- HomeKit 配件类型：`Lightbulb`
- 显示名称前缀：`Home Light`
- HomeKit 服务名：`Light`
- 支持的控制项：`On/Off`、`Brightness`、`Hue`、`Saturation`

## 运行逻辑

- `On/Off` 会调用公共的 `gpio_output_set_on()`
- `Brightness`、`Hue`、`Saturation` 当前会更新内存状态并写回 HomeKit 特征值
- 这几个颜色和亮度参数目前还没有映射到真实 PWM 或多通道灯光输出
- 当前共享硬件输出仍然来自 `devices/common/gpio_output.c`

## 构建选择

- 通过 `HOMEKIT_DEVICE_TYPE=light` 选择这个实现
- 典型命令：`HOMEKIT_DEVICE_TYPE=light idf.py reconfigure build`
