#!/bin/bash
ROOT=$(realpath $(dirname "$0")/..)

if [ -z "$1" ] || [ -z "$2" ]; then
    echo "usage: $0 <device> <app> [args...]"
    echo "available: $(ls $ROOT/devices/$1/out/bins 2>/dev/null | grep -v stratum_binary | tr '\n' ' ')"
    exit 1
fi

bash "$ROOT/scripts/run_app.sh" "$1" "$2" "${@:3}"
