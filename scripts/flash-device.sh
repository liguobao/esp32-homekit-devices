#!/usr/bin/env bash

set -euo pipefail

if [[ $# -lt 1 ]]; then
    echo "Usage: $0 <outlet|light|dashboard|epaper> [idf.py args...]" >&2
    exit 1
fi

DEVICE_TYPE="$1"
shift

case "$DEVICE_TYPE" in
    outlet|light|dashboard|epaper)
        ;;
    *)
        echo "Unsupported device type: $DEVICE_TYPE" >&2
        exit 1
        ;;
esac

has_idf_action() {
    local arg

    for arg in "$@"; do
        case "$arg" in
            all|app|app-flash|bootloader|bootloader-flash|build|clean|dfu|docs|efuse-common-table|erase-flash|encrypted-app-flash|encrypted-flash|encrypted-ota-data-initial|flash|fullclean|menuconfig|monitor|partition-table|partition-table-flash|python-clean|reconfigure|set-target|show-efuse-table|size|size-components|size-files|uf2|uf2-app)
                return 0
                ;;
        esac
    done

    return 1
}

if [[ $# -eq 0 ]]; then
    set -- reconfigure flash
elif ! has_idf_action "$@"; then
    set -- "$@" reconfigure flash
fi

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"

get_idf_target() {
    local config_file="$1"
    [[ -f "$config_file" ]] || return 1
    sed -n 's/^CONFIG_IDF_TARGET="\([^"]*\)"/\1/p' "$config_file" | head -n 1
}

get_default_target_for_device() {
    local device_type="$1"

    case "$device_type" in
        epaper)
            echo "esp32s3"
            ;;
        *)
            get_idf_target "$PROJECT_DIR/sdkconfig.defaults"
            ;;
    esac
}

get_cached_build_target() {
    local cache_file="$1"
    [[ -f "$cache_file" ]] || return 1
    sed -n 's/^IDF_TARGET:STRING=\(.*\)$/\1/p' "$cache_file" | head -n 1
}

get_cached_toolchain_file() {
    local cache_file="$1"
    [[ -f "$cache_file" ]] || return 1
    sed -n 's/^CMAKE_TOOLCHAIN_FILE:FILEPATH=\(.*\)$/\1/p' "$cache_file" | head -n 1
}

IDF_TARGET_VALUE="$(get_idf_target "$PROJECT_DIR/sdkconfig.defaults.local" || true)"
if [[ -z "$IDF_TARGET_VALUE" ]]; then
    IDF_TARGET_VALUE="$(get_default_target_for_device "$DEVICE_TYPE" || true)"
fi
if [[ -z "$IDF_TARGET_VALUE" ]]; then
    IDF_TARGET_VALUE="$(get_idf_target "$PROJECT_DIR/sdkconfig" || true)"
fi

BUILD_TARGET_VALUE="$(get_cached_build_target "$PROJECT_DIR/build/CMakeCache.txt" || true)"
BOOTLOADER_TOOLCHAIN_VALUE="$(get_cached_toolchain_file "$PROJECT_DIR/build/bootloader/CMakeCache.txt" || true)"
EXPECTED_TOOLCHAIN_SUFFIX=""
if [[ -n "$IDF_TARGET_VALUE" ]]; then
    EXPECTED_TOOLCHAIN_SUFFIX="toolchain-${IDF_TARGET_VALUE}.cmake"
fi

if [[ -n "$IDF_TARGET_VALUE" && -n "$BUILD_TARGET_VALUE" && "$IDF_TARGET_VALUE" != "$BUILD_TARGET_VALUE" ]]; then
    rm -rf "$PROJECT_DIR/build"
elif [[ -n "$EXPECTED_TOOLCHAIN_SUFFIX" && -n "$BOOTLOADER_TOOLCHAIN_VALUE" && "$BOOTLOADER_TOOLCHAIN_VALUE" != *"$EXPECTED_TOOLCHAIN_SUFFIX" ]]; then
    rm -rf "$PROJECT_DIR/build"
fi

rm -f "$PROJECT_DIR/sdkconfig" "$PROJECT_DIR/sdkconfig.old"

IDF_ARGS=()
if [[ -n "$IDF_TARGET_VALUE" ]]; then
    IDF_ARGS+=("-DIDF_TARGET=$IDF_TARGET_VALUE")
fi
IDF_ARGS+=("-DHOMEKIT_DEVICE_TYPE=$DEVICE_TYPE")

exec "$SCRIPT_DIR/idf-run.sh" "${IDF_ARGS[@]}" "$@"
