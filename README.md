# ESP32 HomeKit

这是一个基于 Espressif `esp-homekit-sdk` 的 `ESP32` Apple 家庭（HomeKit）示例工程。
当前默认实现是 `Outlet`，工程结构已经按设备类型拆分，后续可以继续扩展更多配件。

## 当前默认配置

- 芯片目标：`ESP32-C3`
- 默认设备类型：`Outlet`
- 默认配件名称：`Home Outlet-XXXX`（按设备 MAC 后两字节生成）
- 默认控制引脚：`GPIO2`
- 默认输出逻辑：`active-low`
- 默认 Wi-Fi 方式：固件内写死，通过本地配置文件覆盖
- HomeKit 配对码：`111-22-333`
- HomeKit 输入码：`11122333`

`active-low` 的含义是：

- 打开配件时，`GPIO2` 拉低
- 关闭配件时，`GPIO2` 拉高

## 工程结构

- 公共 Wi-Fi / HomeKit 启动逻辑：`main/app_main.c`
- 设备选择与分发：`devices/device.c`
- 统一设备接口：`devices/include/device.h`
- `Outlet` 实现：`devices/outlet`
- `Light` 实现：`devices/light`
- 公共 GPIO 输出逻辑：`devices/common`
- 快捷脚本：`scripts`

## 快速开始

1. 准备本地 Wi-Fi 配置。
2. 编译并烧录默认的 `Outlet` 示例。
3. 在 Apple 家庭中添加配件。

最常用的命令是：

```sh
./scripts/flash-outlet.sh -p /dev/cu.usbmodem5ABA0859681
./scripts/monitor.sh -p /dev/cu.usbmodem5ABA0859681
```

## 本地 Wi-Fi 配置

仓库不会提交真实的 Wi-Fi 账号密码。请使用本地覆盖文件：

```sh
cp sdkconfig.defaults.local.example sdkconfig.defaults.local
```

然后编辑 `sdkconfig.defaults.local`，填入你自己的 Wi-Fi：

```ini
CONFIG_APP_WIFI_SSID="your-wifi-ssid"
CONFIG_APP_WIFI_PASSWORD="your-wifi-password"
```

`sdkconfig.defaults.local` 已加入 `.gitignore`，不会进入仓库。

如果你修改了这个文件，建议删除本地 `sdkconfig` 后再重新构建，让新的默认值重新生效：

```sh
rm -f sdkconfig
```

如果构建目标曾被切到别的芯片，或你发现目标不再是 `esp32c3`，再执行：

```sh
rm -rf build
idf.py set-target esp32c3
```

## 快捷脚本

项目内置了一组常用脚本，默认会自动加载本机的 `ESP-IDF` 环境：

- `./scripts/build-outlet.sh`
- `./scripts/build-light.sh`
- `./scripts/flash-outlet.sh -p <PORT>`
- `./scripts/flash-light.sh -p <PORT>`
- `./scripts/monitor.sh -p <PORT>`

默认行为：

- `build-*.sh` 执行 `reconfigure build`
- `flash-*.sh` 执行 `reconfigure flash`
- `monitor.sh` 执行 `monitor`

示例：

```sh
./scripts/build-outlet.sh
./scripts/build-light.sh
./scripts/flash-outlet.sh -p /dev/cu.usbmodem5ABA0859681
./scripts/monitor.sh -p /dev/cu.usbmodem5ABA0859681
```

如果你要传更多 `idf.py` 参数，也可以直接追加：

```sh
./scripts/build-light.sh -p /dev/cu.usbmodem5ABA0859681 reconfigure flash monitor
./scripts/flash-light.sh -p /dev/cu.usbmodem5ABA0859681 erase-flash flash
./scripts/monitor.sh -p /dev/cu.usbmodem5ABA0859681 -B build
```

## 切换设备类型

当前支持的设备类型只有：

- `outlet`
- `light`

除了快捷脚本，也可以直接用 `idf.py` 指定：

```sh
idf.py -DHOMEKIT_DEVICE_TYPE=outlet build
idf.py -DHOMEKIT_DEVICE_TYPE=light build
```

也可以用环境变量：

```sh
HOMEKIT_DEVICE_TYPE=light idf.py reconfigure build
```

## 手动编译与烧录

如果你不想用快捷脚本，也可以直接使用 `idf.py`。

第一次构建、切换过芯片型号，或者你清空了 `build` 目录时，先执行一次：

```sh
export IDF_PATH=$HOME/.espressif/frameworks/esp-idf-v5.4.2
. "$IDF_PATH/export.sh"
idf.py set-target esp32c3
```

完整示例：构建并烧录 `light`

```sh
export IDF_PATH=$HOME/.espressif/frameworks/esp-idf-v5.4.2
. "$IDF_PATH/export.sh"
idf.py set-target esp32c3
HOMEKIT_DEVICE_TYPE=light idf.py reconfigure build
idf.py -p /dev/cu.usbmodem5ABA0859681 flash monitor
```

完整示例：构建并烧录 `outlet`

```sh
export IDF_PATH=$HOME/.espressif/frameworks/esp-idf-v5.4.2
. "$IDF_PATH/export.sh"
idf.py set-target esp32c3
HOMEKIT_DEVICE_TYPE=outlet idf.py reconfigure build
idf.py -p /dev/cu.usbmodem5ABA0859681 flash monitor
```

如果当前 `build` 目录已经是你要的设备类型，也可以直接跳过 `reconfigure build`，只执行：

```sh
idf.py -p /dev/cu.usbmodem5ABA0859681 flash monitor
```

只重新烧录：

```sh
export IDF_PATH=$HOME/.espressif/frameworks/esp-idf-v5.4.2
. "$IDF_PATH/export.sh"
idf.py -p /dev/cu.usbmodem5ABA0859681 flash
```

只查看串口日志：

```sh
export IDF_PATH=$HOME/.espressif/frameworks/esp-idf-v5.4.2
. "$IDF_PATH/export.sh"
idf.py -p /dev/cu.usbmodem5ABA0859681 monitor
```

如果 `monitor` 提示找不到 `.elf`，先执行一次 `idf.py build`，这样可以保留符号信息用于解析日志。

清空配对信息并重新添加到 Apple 家庭：

```sh
export IDF_PATH=$HOME/.espressif/frameworks/esp-idf-v5.4.2
. "$IDF_PATH/export.sh"
idf.py -p /dev/cu.usbmodem5ABA0859681 erase-flash flash
```

## 添加到 Apple 家庭

1. 等待设备连上 Wi-Fi。
2. 确认 iPhone 或 iPad 与设备处于同一局域网。
3. 打开“家庭”App。
4. 选择“添加配件”。
5. 优先扫描 HomeKit 二维码，或打开下方链接查看二维码。
6. 如果无法扫码，选择“没有代码或无法扫描”。
7. 在附近配件里选择 `Home Outlet-XXXX`。
8. 接受“未认证配件”提示。
9. 输入配对码：`11122333`。

[HomeKit 配对二维码](https://espressif.github.io/esp-homekit-sdk/qrcode.html?data=X-HM://00718C331C3HK)

## 接线说明

- 负载一端接 `3.3V`
- 负载另一端接 `GPIO2`
- 当前默认是低电平导通，所以在 Apple 家庭中“打开”配件时，`GPIO2` 会被拉低

如果你接的是普通 LED，请确认已经串联限流电阻。
如果你接的是灯板、模块或其他负载，建议使用三极管或 MOSFET 做驱动，不要长期直接由 GPIO 口带载。
