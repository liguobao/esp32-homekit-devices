# AGENTS.md — ESP32 HomeKit

本文件供 OpenAI Codex CLI 与其他 AI 编码 Agent 读取，描述本仓库的结构、构建方式与编码约定。

---

## 项目概览

基于 Espressif `esp-homekit-sdk` 的 ESP32-C3 HomeKit 工程，支持 `outlet`、`light`、`dashboard` 三种设备类型，通过编译时宏 `HOMEKIT_DEVICE_TYPE` 切换。

- **目标芯片**：`ESP32-C3`
- **工具链**：`ESP-IDF 5.4.2`，默认路径 `$HOME/.espressif/frameworks/esp-idf-v5.4.2`
- **HomeKit 协议栈**：`third_party/esp-homekit-sdk`（vendored）
- **默认设备**：`outlet`
- **GPIO 输出**：`GPIO2`，逻辑为 `active-low`
- **默认配对码**：`111-22-333`

---

## 目录结构

```
main/app_main.c          # 共享 Wi-Fi 与 HomeKit 启动逻辑（勿按设备复制）
devices/device.c         # 当前设备分发逻辑
devices/<type>/          # 各设备类型实现（outlet / light / dashboard）
devices/common/          # 共享硬件辅助模块
devices/include/         # 公共头文件
scripts/                 # 构建、烧录、监控脚本
docs/                    # 核心项目文档
third_party/esp-homekit-sdk/   # vendored HomeKit 协议栈
managed_components/      # ESP Component Registry 组件（勿手动编辑）
skills/                  # 仓库内 AI 工作流技能定义
```

---

## 构建与烧录

### 前置步骤

```sh
# 复制本地 Wi-Fi 配置（不要提交）
cp sdkconfig.defaults.local.example sdkconfig.defaults.local
# 编辑 sdkconfig.defaults.local，填写真实 Wi-Fi SSID/密码
```

### 常用命令（macOS / Linux）

```sh
# 构建
./scripts/build-device.sh outlet
./scripts/build-device.sh light
./scripts/build-device.sh dashboard

# 烧录（替换 /dev/cu.usbmodemXXXX 为实际端口）
./scripts/flash-device.sh outlet  -p /dev/cu.usbmodemXXXX
./scripts/flash-device.sh light   -p /dev/cu.usbmodemXXXX
./scripts/flash-device.sh dashboard -p /dev/cu.usbmodemXXXX

# 串口监控
./scripts/monitor.sh -p /dev/cu.usbmodemXXXX
```

### Windows (PowerShell)

```powershell
.\scripts\flash-device.ps1 outlet -p COM5
```

### 直接使用 idf.py（不走脚本）

```sh
# 先删除旧配置，再重新配置并构建
rm -f sdkconfig sdkconfig.old
idf.py -DHOMEKIT_DEVICE_TYPE=outlet reconfigure build

# 烧录
idf.py -p /dev/cu.usbmodemXXXX flash

# 监控
idf.py -p /dev/cu.usbmodemXXXX monitor
```

> **注意**：`build-device.sh` 在每次执行前会删除 `sdkconfig` / `sdkconfig.old`，并保留芯片目标设置，确保本地覆盖配置生效。直接使用 `idf.py` 时你需要手动处理这些清理步骤。

---

## 编码约定

1. **共享启动逻辑**：Wi-Fi 初始化和 HomeKit 启动流程只放在 `main/app_main.c`，不要在每个设备目录复制一份。
2. **设备隔离**：每种设备类型的特有逻辑放在 `devices/<type>/`。
3. **设备切换**：始终以 `HOMEKIT_DEVICE_TYPE` 作为构建时切换开关。
4. **GPIO 默认值**：共享输出脚为 `GPIO2`，`active-low`；如有修改请同步更新 `docs/core.md` 和 `docs/core.en.md`。
5. **Wi-Fi 凭据**：只写入被 `.gitignore` 忽略的本地文件（`sdkconfig.defaults.local`），不要提交到仓库。
6. **文档语言**：面向用户的文档默认中文，英文版放在对应的 `.en.md` 文件（如 `README.en.md`、`docs/core.en.md`）。
7. **根 README**：保持精简，详细内容放到 `docs/`、`scripts/README.md`、`devices/README.md`。
8. **managed_components**：不要手动编辑，由 ESP Component Registry 管理。

---

## 添加新设备类型

1. 阅读 `devices/README.md` 了解现有设备实现。
2. 在 `devices/<name>/` 下创建新设备实现（参考 `devices/outlet/` 或 `devices/light/`）。
3. 更新 `devices/CMakeLists.txt`，将新目录加入构建。
4. 在 `devices/device.c` 中添加对新类型的分发逻辑。
5. 在顶层 `CMakeLists.txt` 中确认新类型已被包含。
6. 在 `scripts/build-device.sh` 的 `case` 分支里添加新类型名称。
7. 在 `devices/README.md` 和 `devices/README.en.md` 中补充说明。

---

## 常见问题

| 问题 | 解决方法 |
|------|---------|
| 构建失败，提示找不到 `idf.py` | 确认已安装 ESP-IDF 5.4.2 并执行过 `. $IDF_PATH/export.sh` |
| 芯片目标不匹配导致构建报错 | 删除 `build/` 目录后重新执行 `build-device.sh` |
| Wi-Fi 无法连接 | 检查 `sdkconfig.defaults.local` 里的 SSID/密码是否正确 |
| HomeKit 配对失败 | 确认手机与设备处于同一局域网，使用配对码 `11122333` |
| `sdkconfig` 改动不生效 | 脚本会删除旧 `sdkconfig`，修改 `sdkconfig.defaults.local` 即可生效 |

---

## 参考资源

- ESP-IDF 文档：<https://docs.espressif.com/projects/esp-idf/en/stable/esp32c3/>
- esp-homekit-sdk：<https://github.com/espressif/esp-homekit-sdk>
- Espressif Component Registry：<https://components.espressif.com/>
- HomeKit 配对二维码生成：<https://espressif.github.io/esp-homekit-sdk/qrcode.html?data=X-HM://00718C331C3HK>
