#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"

if [[ -z "${IDF_PATH:-}" ]]; then
    IDF_PATH="$HOME/.espressif/frameworks/esp-idf-v5.4.2"
fi

quote_cmd_arg() {
    local value="$1"
    value="${value//\"/\"\"}"
    if [[ "$value" =~ [[:space:]\"] ]]; then
        printf '"%s"' "$value"
    else
        printf '%s' "$value"
    fi
}

case "${MSYSTEM:-}:$(uname -s 2>/dev/null || true)" in
    *MINGW*|*MSYS*|*CYGWIN*)
        if [[ ! -f "$IDF_PATH/export.bat" ]]; then
            echo "ESP-IDF not found: $IDF_PATH/export.bat" >&2
            exit 1
        fi
        if ! command -v cygpath >/dev/null 2>&1; then
            echo "cygpath is required for Windows Git Bash support" >&2
            exit 1
        fi

        EXPORT_BAT_WIN="$(cygpath -w "$IDF_PATH/export.bat")"
        CMD_LINE='set "MSYSTEM=" && '
        CMD_LINE+="$(quote_cmd_arg "$EXPORT_BAT_WIN") && idf.py"
        for arg in "$@"; do
            CMD_LINE+=" $(quote_cmd_arg "$arg")"
        done

        cd "$PROJECT_DIR"
        exec cmd.exe /c "$CMD_LINE"
        ;;
esac

if [[ ! -f "$IDF_PATH/export.sh" ]]; then
    echo "ESP-IDF not found: $IDF_PATH/export.sh" >&2
    exit 1
fi

# shellcheck disable=SC1090
. "$IDF_PATH/export.sh"

cd "$PROJECT_DIR"
exec idf.py "$@"
