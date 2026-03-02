#!/usr/bin/env bash

set -euo pipefail

if [[ $# -lt 1 ]]; then
    echo "Usage: $0 <outlet|light> [idf.py args...]" >&2
    exit 1
fi

DEVICE_TYPE="$1"
shift

case "$DEVICE_TYPE" in
    outlet|light)
        ;;
    *)
        echo "Unsupported device type: $DEVICE_TYPE" >&2
        exit 1
        ;;
esac

if [[ $# -eq 0 ]]; then
    set -- reconfigure flash
fi

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"
rm -f "$PROJECT_DIR/sdkconfig" "$PROJECT_DIR/sdkconfig.old"
exec "$SCRIPT_DIR/idf-run.sh" -DHOMEKIT_DEVICE_TYPE="$DEVICE_TYPE" "$@"
