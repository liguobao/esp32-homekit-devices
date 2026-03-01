# 核心说明

## 当前默认配置

- 目标芯片：`ESP32-C3`
- 默认设备类型：`Outlet`
- 默认配件名前缀：`Home Outlet`
- 默认输出脚：`GPIO2`
- 输出逻辑：`active-low`
- Wi-Fi 配置方式：通过本地覆盖文件写死
- HomeKit 配对码：`111-22-333`

`active-low` 表示配件打开时，输出脚会被拉低。

## 项目结构

- `main/app_main.c`
  共享 Wi-Fi 与 HomeKit 启动逻辑
- `devices/`
  设备实现与公共辅助模块
- `scripts/`
  本地构建、烧录、监控脚本
- `skills/`
  仓库内 AI 工作流技能

## 核心依赖

- `ESP-IDF 5.4.2`
  提供芯片支持、构建系统、驱动、Wi-Fi、NVS 和日志能力
- `esp-homekit-sdk`
  以 vendored 方式放在 `third_party/esp-homekit-sdk`，作为 HomeKit 协议栈
- Espressif Component Registry 组件
  通过 `dependencies.lock` 锁定，包括 `mdns`、`libsodium`、`json_parser`、`json_generator`、`jsmn`

## 添加到 Apple 家庭

1. 等待设备连上 Wi-Fi。
2. 确保 iPhone 或 iPad 与设备处于同一局域网。
3. 打开“家庭”App 并选择“添加配件”。
4. 扫描二维码，或选择“没有代码或无法扫描”。
5. 选择附近的 `Home Outlet-XXXX`。
6. 输入配对码 `11122333`。

二维码链接：

[HomeKit 配对二维码](https://espressif.github.io/esp-homekit-sdk/qrcode.html?data=X-HM://00718C331C3HK)

## 接线说明

- 负载一端接 `3.3V`
- 负载另一端接 `GPIO2`
- 当前默认是低电平导通

如果是普通 LED，请串联限流电阻。
如果是灯板、模块或更大负载，建议加三极管或 MOSFET。

## 鸣谢与上游项目

- `ESP-IDF`: `https://github.com/espressif/esp-idf`
- `esp-homekit-sdk`: `https://github.com/espressif/esp-homekit-sdk`
- Espressif Component Registry: `https://components.espressif.com/`

感谢 Espressif 以及相关开源项目的维护者和贡献者。
