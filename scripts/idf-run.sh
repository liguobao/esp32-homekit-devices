#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"

if [[ -z "${IDF_PATH:-}" ]]; then
    IDF_PATH="$HOME/.espressif/frameworks/esp-idf-v5.4.2"
fi

if [[ ! -f "$IDF_PATH/export.sh" ]]; then
    echo "ESP-IDF not found: $IDF_PATH/export.sh" >&2
    exit 1
fi

# shellcheck disable=SC1090
. "$IDF_PATH/export.sh"

cd "$PROJECT_DIR"
exec idf.py "$@"
