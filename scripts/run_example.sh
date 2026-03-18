#!/bin/bash
ROOT=$(realpath $(dirname "$0")/..)

if [ -z "$1" ] || [ -z "$2" ]; then
    echo "usage: $0 <device> <app>"
    exit 1
fi

DEVICE=$1
APP=$2
DEVICE_DIR="$ROOT/devices/$DEVICE"
OUT_BINS="$DEVICE_DIR/out/bins"
OUT_LIBS="$DEVICE_DIR/out/libs"

if [ ! -d "$DEVICE_DIR" ]; then
    echo "error: '$DEVICE_DIR' not found"
    exit 1
fi

if [ ! -f "$OUT_BINS/$APP" ]; then
    echo "error: '$APP' not found in $OUT_BINS"
    echo "available: $(ls $OUT_BINS | grep -v stratum_binary | tr '\n' ' ')"
    exit 1
fi

su -c "EXTRAS_DIR=$OUT_BINS \
       LD_PRELOAD=$OUT_LIBS/stub.so \
       LD_LIBRARY_PATH=$OUT_LIBS:/system/lib64:/vendor/lib64 \
       $OUT_BINS/$APP ${@:3}"
