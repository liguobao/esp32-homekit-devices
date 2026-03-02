# 脚本

这个目录放的是本项目常用的本地开发脚本，目标是减少重复输入并统一 `ESP-IDF` 的调用方式。
英文版说明见 [README.en.md](README.en.md)。

## 常用脚本

- `idf-run.sh`
  公共入口，负责加载 `ESP-IDF` 环境并执行 `idf.py`
- `idf-run.ps1`
  Windows PowerShell 公共入口，负责加载 `ESP-IDF` 环境并执行 `idf.py`
- `build-device.sh`
  通用构建入口，按设备类型执行 `reconfigure build`
- `flash-device.sh`
  通用烧录入口，按设备类型执行 `reconfigure flash`
- `flash-device.ps1`
  Windows PowerShell 烧录入口，按设备类型执行 `reconfigure flash`
- `monitor.sh`
  打开串口日志监视

## 常用命令

```sh
./scripts/build-device.sh outlet
./scripts/build-device.sh light
./scripts/flash-device.sh outlet -p /dev/cu.usbmodemXXXX
./scripts/flash-device.sh light -p /dev/cu.usbmodemXXXX
./scripts/monitor.sh -p /dev/cu.usbmodemXXXX
```

Windows PowerShell：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\flash-device.ps1 outlet -p COM5
powershell -ExecutionPolicy Bypass -File .\scripts\idf-run.ps1 --version
```

如果要透传更多 `idf.py` 参数，可以直接追加：

```sh
./scripts/build-device.sh light -p /dev/cu.usbmodemXXXX reconfigure flash monitor
./scripts/flash-device.sh light -p /dev/cu.usbmodemXXXX erase-flash flash
./scripts/monitor.sh -p /dev/cu.usbmodemXXXX -B build
```

`build-device.sh`、`flash-device.sh` 和 `flash-device.ps1` 会在执行前删除
`sdkconfig` 与 `sdkconfig.old`，这样 `sdkconfig.defaults.local` 的最新改动
会被重新生成到新的配置文件里。

## 编写新脚本

建议遵循这些约定：

1. 使用 `bash`，并加上 `set -euo pipefail`
2. 优先复用 `idf-run.sh`，不要在每个脚本里重复写环境初始化
3. 文件名按动作命名，例如 `build-*`、`flash-*`、`monitor-*`
4. 不要把端口号、Wi-Fi 凭据之类的本地信息写死在脚本里
5. 让“默认行为”清晰，比如没有参数时执行什么命令

最小模板：

```bash
#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
exec "$SCRIPT_DIR/idf-run.sh" "$@"
```
