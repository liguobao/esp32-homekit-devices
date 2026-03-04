#!/usr/bin/env bash

set -euo pipefail

if [[ $# -lt 1 ]]; then
    echo "Usage: $0 <outlet|light|dashboard> [idf.py args...]" >&2
    exit 1
fi

DEVICE_TYPE="$1"
shift

case "$DEVICE_TYPE" in
    outlet|light|dashboard)
        ;;
    *)
        echo "Unsupported device type: $DEVICE_TYPE" >&2
        exit 1
        ;;
esac

if [[ $# -eq 0 ]]; then
    set -- reconfigure build
fi

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"

get_idf_target() {
    local config_file="$1"
    [[ -f "$config_file" ]] || return 1
    sed -n 's/^CONFIG_IDF_TARGET="\([^"]*\)"/\1/p' "$config_file" | head -n 1
}

IDF_TARGET_VALUE="$(get_idf_target "$PROJECT_DIR/sdkconfig" || true)"
if [[ -z "$IDF_TARGET_VALUE" ]]; then
    IDF_TARGET_VALUE="$(get_idf_target "$PROJECT_DIR/sdkconfig.defaults" || true)"
fi

rm -f "$PROJECT_DIR/sdkconfig" "$PROJECT_DIR/sdkconfig.old"

IDF_ARGS=()
if [[ -n "$IDF_TARGET_VALUE" ]]; then
    IDF_ARGS+=("-DIDF_TARGET=$IDF_TARGET_VALUE")
fi
IDF_ARGS+=("-DHOMEKIT_DEVICE_TYPE=$DEVICE_TYPE")

exec "$SCRIPT_DIR/idf-run.sh" "${IDF_ARGS[@]}" "$@"
