#!/bin/bash
set -e
ROOT=$(realpath $(dirname $0)/..)
OUT=$ROOT/stratum-boot.zip
cd $ROOT/stratum-boot
zip -r $OUT .
echo "[*] Module built: $OUT"
