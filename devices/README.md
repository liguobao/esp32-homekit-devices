# 设备

这个目录放的是不同 HomeKit 设备类型的实现，以及它们共享的公共逻辑。
英文版说明见 [README.en.md](README.en.md)。

## 当前结构

- `common/`
  共享硬件辅助逻辑，例如当前的 GPIO 输出封装
- `include/device.h`
  统一设备接口定义
- `device.c`
  当前激活设备的分发逻辑
- `<type>/`
  每种设备一个目录，例如 `outlet/`、`light/`、`dashboard/`

## 如何新增一个设备

1. 新建目录 `devices/<name>/`，并创建 `<name>_device.c` 与 `<name>_device.h`
2. 参考现有实现，导出一个 `const homekit_device_t *<name>_device_get(void)`
3. 在设备实现里补齐 `name_prefix`、`cid`、`identify`、`add_services`、`init_hardware`
4. 如果能复用现有输出逻辑，就调用 `devices/common/gpio_output.c`；如果不够用，再补新的公共辅助模块
5. 在 `devices/CMakeLists.txt` 里加入新源文件和私有头文件目录
6. 在 `devices/device.c` 里加入新的分发逻辑
7. 在顶层 `CMakeLists.txt` 里扩展 `HOMEKIT_DEVICE_TYPE` 的合法值校验
8. 为新设备补一份 `devices/<name>/README.md`，如需要再补 `README.en.md`

## 当前切换方式

当前工程内置了 `outlet`、`light` 和 `dashboard` 三种设备类型，构建时通过 `HOMEKIT_DEVICE_TYPE` 切换。
示例：

```sh
HOMEKIT_DEVICE_TYPE=outlet idf.py reconfigure build
HOMEKIT_DEVICE_TYPE=light idf.py reconfigure build
HOMEKIT_DEVICE_TYPE=dashboard idf.py reconfigure build
```
