---
name: vibe-coding
description: Use when changing this ESP32 HomeKit repository, especially for device additions, HomeKit behavior, docs, or build and flash workflow updates.
---

# Vibe Coding

Use this skill when working inside this repository.

## Quick Start

- Read `references/project.md` first for repo layout and constraints.
- Keep root `README.md` minimal; move detailed docs into `docs/`, `scripts/README.md`, or `devices/README.md`.
- Reuse the existing shared bootstrap in `main/app_main.c` and keep device behavior in `devices/<type>/`.
- Prefer `scripts/build-device.sh`, `scripts/flash-device.sh`, `scripts/flash-device.ps1`, and `scripts/monitor.sh` for common local tasks.
