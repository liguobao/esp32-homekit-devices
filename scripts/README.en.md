# Scripts

This directory contains local helper scripts for common development tasks.
The goal is to reduce repeated typing and standardize `ESP-IDF` invocation.

## Common Scripts

- `idf-run.sh`
  Shared entry point that loads the `ESP-IDF` environment and runs `idf.py`
- `build-device.sh`
  Generic build wrapper that runs `reconfigure build` by device type
- `build-outlet.sh`
  Builds the `outlet` variant
- `build-light.sh`
  Builds the `light` variant
- `flash-device.sh`
  Generic flash wrapper that runs `reconfigure flash` by device type
- `flash-outlet.sh`
  Flashes the `outlet` variant
- `flash-light.sh`
  Flashes the `light` variant
- `monitor.sh`
  Opens the serial monitor

## Typical Usage

```sh
./scripts/build-outlet.sh
./scripts/build-light.sh
./scripts/flash-outlet.sh -p /dev/cu.usbmodemXXXX
./scripts/flash-light.sh -p /dev/cu.usbmodemXXXX
./scripts/monitor.sh -p /dev/cu.usbmodemXXXX
```

You can append extra `idf.py` arguments directly:

```sh
./scripts/build-light.sh -p /dev/cu.usbmodemXXXX reconfigure flash monitor
./scripts/flash-light.sh -p /dev/cu.usbmodemXXXX erase-flash flash
./scripts/monitor.sh -p /dev/cu.usbmodemXXXX -B build
```

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
