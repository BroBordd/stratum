#!/bin/bash
ROOT=$(realpath $(dirname "$0")/..)

if [ -z "$1" ]; then
    echo "usage: $0 <device>"
    exit 1
fi

DEVICE=$1
DEVICE_DIR="$ROOT/devices/$DEVICE"
OUT_BINS="$DEVICE_DIR/out/bins"
OUT_LIBS="$DEVICE_DIR/out/libs"

if [ ! -d "$DEVICE_DIR" ]; then
    echo "error: '$DEVICE_DIR' not found"
    exit 1
fi

su -c "LD_PRELOAD=$OUT_LIBS/stub.so \
       LD_LIBRARY_PATH=$OUT_LIBS:/system/lib64:/vendor/lib64 \
       $OUT_BINS/stratum_binary"
