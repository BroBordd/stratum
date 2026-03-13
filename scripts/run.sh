#!/bin/bash
BIN=$(dirname "$0")/../bin
su -c "LD_PRELOAD=$BIN/stub.so LD_LIBRARY_PATH=/system/lib64:/vendor/lib64 $BIN/stratum"
