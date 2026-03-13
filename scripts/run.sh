#!/bin/bash
ROOT=$(realpath $(dirname "$0")/..)
su -c "LD_PRELOAD=$ROOT/bin/stub.so LD_LIBRARY_PATH=/system/lib64:/vendor/lib64 timeout 5 $ROOT/bin/stratum"
