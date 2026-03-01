# 项目参考

## 基本信息

- 目标芯片：`ESP32-C3`
- 工具链：`ESP-IDF 5.4.2`
- HomeKit 协议栈：vendored `third_party/esp-homekit-sdk`
- 默认设备类型：`outlet`
- 构建切换方式：`HOMEKIT_DEVICE_TYPE=outlet|light`

## 目录约定

- `main/app_main.c`：共享 Wi-Fi 与 HomeKit 启动逻辑
- `devices/device.c`：当前设备分发
- `devices/<type>/`：设备类型实现
- `devices/common/`：公共硬件辅助逻辑
- `scripts/`：本地构建、烧录、监控脚本
- `docs/`：核心项目文档

## 约束

- 不要提交真实 Wi-Fi 凭据，只放在被忽略的本地文件里
- 共享 Wi-Fi / HomeKit 流程放在 `main/app_main.c`，不要按设备复制
- 设备特有逻辑放在 `devices/<type>/`
- 设备切换继续以 `HOMEKIT_DEVICE_TYPE` 为主
- 当前共享 GPIO 输出是 `GPIO2` 且为 `active-low`，如有变化要同步更新文档
- 根 `README.md` 保持精简，详细内容放到 `docs/`、`scripts/README.md`、`devices/README.md`
- 面向用户的文档默认用中文，英文放在独立的 `.en.md` 文件里

## 常见改动

- 新增设备：
  先看 `devices/README.md`，再补 `devices/<name>/`，并更新 `devices/CMakeLists.txt`、`devices/device.c` 和顶层 `CMakeLists.txt`

- 更新文档：
  中文内容放在主 `.md` 文件里，英文内容放在对应的 `.en.md` 文件里

- 日常本地操作：
  优先使用 `scripts/build-*.sh`、`scripts/flash-*.sh` 和 `scripts/monitor.sh`
