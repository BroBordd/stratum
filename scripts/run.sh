#!/bin/bash
ROOT=$(realpath $(dirname "$0")/..)

if [ -z "$1" ]; then
    echo "usage: $0 <device>"
    exit 1
fi

bash "$ROOT/scripts/run_app.sh" "$1" stratum_binary
