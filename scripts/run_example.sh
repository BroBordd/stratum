#!/bin/bash
ROOT=$(realpath $(dirname "$0")/..)

if [ -z "$1" ]; then
    echo "Usage: bash scripts/run_example.sh <example>"
    echo "Available: $(ls $ROOT/bin | grep -v stub.so | grep -v libstratum.so | tr '\n' ' ')"
    exit 1
fi

su -c "EXTRAS_DIR=$ROOT/bin LD_PRELOAD=$ROOT/bin/stub.so LD_LIBRARY_PATH=$ROOT/bin:/system/lib64:/vendor/lib64 $ROOT/bin/$1 ${@:2}"
