# 插座设备

这个目录包含当前默认的 `Outlet` 设备实现。
英文版说明见 [README.en.md](README.en.md)。

## 主要文件

- `outlet_device.c`
  `Outlet` 服务创建、写回调、设备元信息
- `outlet_device.h`
  对外导出 `outlet_device_get()`

## 当前行为

- HomeKit 配件类型：`Outlet`
- 显示名称前缀：`Home Outlet`
- HomeKit 服务名：`Outlet`
- 支持的主要控制项：`On/Off`

## 运行逻辑

- 当收到 `On` 写入时，会调用公共的 `gpio_output_set_on()`
- 当前共享硬件输出在 `devices/common/gpio_output.c`
- 默认输出脚为 `GPIO2`
- 默认逻辑为 `active-low`

## 构建选择

- 通过 `HOMEKIT_DEVICE_TYPE=outlet` 选择这个实现
- 如果未显式指定设备类型，当前工程默认也是 `outlet`
