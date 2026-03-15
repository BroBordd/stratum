#!/bin/bash
ROOT=$(realpath $(dirname "$0")/..)

if [ -z "$1" ]; then
    echo "Usage: bash scripts/run_example.sh <example>"
    echo "       bash scripts/run_example.sh --default"
    echo "Available: $(ls $ROOT/bin | grep -v stub.so | grep -v libstratum.so | tr '\n' ' ')"
    exit 1
fi

if [ "$1" = "--default" ]; then
    BIN=$ROOT/stratum-boot/system/bin/stratum_binary
    LIBS=$ROOT/stratum-boot/system/lib64
    su -c "LD_PRELOAD=$LIBS/stub.so LD_LIBRARY_PATH=$LIBS:/system/lib64:/vendor/lib64 $BIN ${@:2}"
else
    su -c "EXTRAS_DIR=$ROOT/bin LD_PRELOAD=$ROOT/bin/stub.so LD_LIBRARY_PATH=$ROOT/bin:/system/lib64:/vendor/lib64 $ROOT/bin/$1 ${@:2}"
fi
