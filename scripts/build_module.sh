#!/bin/bash
set -e
ROOT=$(realpath $(dirname $0)/..)

echo "[*] Copying binaries..."
cp $ROOT/bin/signal          $ROOT/stratum-boot/system/bin/stratum_binary
cp $ROOT/bin/libstratum.so   $ROOT/stratum-boot/system/lib64/libstratum.so
cp $ROOT/bin/stub.so         $ROOT/stratum-boot/system/lib64/stub.so
rm -f $ROOT/stratum-boot/system/bin/bootanimation

OUT=$ROOT/stratum-boot.zip
cd $ROOT/stratum-boot
zip -r $OUT .
echo "[*] Module built: $OUT"
