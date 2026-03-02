# Scripts

This directory contains local helper scripts for common development tasks.
The goal is to reduce repeated typing and standardize `ESP-IDF` invocation.

## Common Scripts

- `idf-run.sh`
  Shared entry point that loads the `ESP-IDF` environment and runs `idf.py`
- `idf-run.ps1`
  Shared Windows PowerShell entry point that loads the `ESP-IDF` environment and runs `idf.py`
- `build-device.sh`
  Generic build wrapper that runs `reconfigure build` by device type
- `flash-device.sh`
  Generic flash wrapper that runs `reconfigure flash` by device type
- `flash-device.ps1`
  Windows PowerShell flash wrapper that runs `reconfigure flash` by device type
- `monitor.sh`
  Opens the serial monitor

## Typical Usage

```sh
./scripts/build-device.sh outlet
./scripts/build-device.sh light
./scripts/flash-device.sh outlet -p /dev/cu.usbmodemXXXX
./scripts/flash-device.sh light -p /dev/cu.usbmodemXXXX
./scripts/monitor.sh -p /dev/cu.usbmodemXXXX
```

Windows PowerShell:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\flash-device.ps1 outlet -p COM5
powershell -ExecutionPolicy Bypass -File .\scripts\idf-run.ps1 --version
```

You can append extra `idf.py` arguments directly:

```sh
./scripts/build-device.sh light -p /dev/cu.usbmodemXXXX reconfigure flash monitor
./scripts/flash-device.sh light -p /dev/cu.usbmodemXXXX erase-flash flash
./scripts/monitor.sh -p /dev/cu.usbmodemXXXX -B build
```

`build-device.sh`, `flash-device.sh`, and `flash-device.ps1` delete
`sdkconfig` and `sdkconfig.old` before running, while preserving the current
target from `sdkconfig` when available, so the latest `sdkconfig.defaults.local`
values are always regenerated into a fresh config.

## How to Add a New Script

1. Use `bash` with `set -euo pipefail`
2. Reuse `idf-run.sh` instead of duplicating environment setup
3. Use action-first names such as `build-*`, `flash-*`, or `monitor-*`
4. Do not hardcode local ports or Wi-Fi credentials
5. Make the default behavior explicit when no extra args are given

Minimal template:

```bash
#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
exec "$SCRIPT_DIR/idf-run.sh" "$@"
```
