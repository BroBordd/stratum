#!/bin/bash
set -e

OUT=bin
ROOT=$(dirname $0)/..
mkdir -p $OUT

usage() {
    echo "Usage: $0 [options] [example...]"
    echo "  -l, --lib-only       build libstratum.so only, skip apps"
    echo "  -e, --examples-only  skip rebuilding lib (apps only)"
    echo "  -h, --help           show this help"
    echo ""
    echo "  example args: specific app name(s) to build (default: all)"
    echo "  e.g: $0 terminal brickbreaker"
    exit 0
}

BUILD_LIB=1
BUILD_EXAMPLES=1
ONLY=()

while [[ $# -gt 0 ]]; do
    case $1 in
        -l|--lib-only)       BUILD_EXAMPLES=0; shift ;;
        -e|--examples-only)  BUILD_LIB=0;      shift ;;
        -h|--help)           usage ;;
        -*) echo "Unknown option: $1"; usage ;;
        *)  ONLY+=("$1");    shift ;;
    esac
done

INCLUDES="\
  -I$ROOT/include \
  -I$ROOT/include/v34/arm64/include/frameworks/native/libs/gui/include \
  -I$ROOT/include/v34/arm64/include/frameworks/native/libs/binder/include \
  -I$ROOT/include/v34/arm64/include/frameworks/native/libs/ui/include \
  -I$ROOT/include/v34/arm64/include/frameworks/native/libs/math/include \
  -I$ROOT/include/v34/arm64/include/frameworks/native/libs/nativewindow/include \
  -I$ROOT/include/v34/arm64/include/frameworks/native/libs/nativebase/include \
  -I$ROOT/include/v34/arm64/include/frameworks/native/opengl/include \
  -I$ROOT/include/v34/arm64/include/system/libbase/include \
  -I$ROOT/include/v34/arm64/include/system/core/libutils/include \
  -I$ROOT/include/v34/arm64/include/system/core/libcutils/include \
  -I$ROOT/include/v34/arm64/include/system/core/libsystem/include \
  -I$ROOT/include/v34/arm64/include/system/libhidl/base/include \
  -I$ROOT/include/v34/arm64/include/system/libhidl/transport/token/1.0/utils/include \
  -I$ROOT/include/v34/arm64/include/system/libfmq/base \
  -I$ROOT/include/v34/arm64/include/hardware/libhardware/include \
  -I$ROOT/include/v34/arm64/include/generated-headers/frameworks/native/libs/gui/libgui_aidl_static/android_vendor.34_arm64_armv8-a_static/gen/aidl \
  -I$ROOT/include/v34/arm64/include/generated-headers/frameworks/native/libs/gui/libgui/android_vendor.34_arm64_armv8-a_shared/gen/aidl \
  -I$ROOT/include/v34/arm64/include/generated-headers/frameworks/native/libs/gui/libgui_window_info_static/android_vendor.34_arm64_armv8-a_static_afdo-libgui_lto-thin/gen/aidl \
  -I$ROOT/include/v34/arm64/include/generated-headers/hardware/interfaces/graphics/common/aidl/android.hardware.graphics.common-V4-ndk-source/gen/include \
  -I$ROOT/include/v34/arm64/include/generated-headers/hardware/interfaces/graphics/common/1.0/android.hardware.graphics.common@1.0_genc++_headers/gen \
  -I$ROOT/include/v34/arm64/include/generated-headers/hardware/interfaces/graphics/common/1.1/android.hardware.graphics.common@1.1_genc++_headers/gen \
  -I$ROOT/include/v34/arm64/include/generated-headers/hardware/interfaces/graphics/common/1.2/android.hardware.graphics.common@1.2_genc++_headers/gen \
  -I$ROOT/include/v34/arm64/include/generated-headers/hardware/interfaces/graphics/bufferqueue/1.0/android.hardware.graphics.bufferqueue@1.0_genc++_headers/gen \
  -I$ROOT/include/v34/arm64/include/generated-headers/hardware/interfaces/graphics/bufferqueue/2.0/android.hardware.graphics.bufferqueue@2.0_genc++_headers/gen \
  -I$ROOT/include/v34/arm64/include/generated-headers/system/libhidl/transport/base/1.0/android.hidl.base@1.0_genc++_headers/gen \
  -I$ROOT/include/v34/arm64/include/generated-headers/system/libhidl/transport/manager/1.0/android.hidl.manager@1.0_genc++_headers/gen \
  -I$ROOT/include/v34/arm64/include/generated-headers/hardware/interfaces/media/1.0/android.hardware.media@1.0_genc++_headers/gen \
  -I$ROOT/include/v34/arm64/include/system/libhwbinder/include \
  -I$ROOT/include/logging/liblog/include"

FLAGS="-std=c++17 -O2 -march=armv8-a -target aarch64-linux-android34 -D__BIONIC__ -w -include $ROOT/include/compat.h"
LIBS="-L/system/lib64 -lgui -lui -lEGL -lGLESv2 -lbinder -lutils -llog -Wl,--allow-shlib-undefined -Wl,--unresolved-symbols=ignore-all"

if [[ $BUILD_LIB -eq 1 ]]; then
    echo "[*] Building stub..."
    cat > $ROOT/include/stub.cpp << 'STUB'
#include <typeinfo>
namespace android {
    class VectorImpl { public: virtual ~VectorImpl() {} };
    class SortedVectorImpl : public VectorImpl { public: virtual ~SortedVectorImpl() {} };
}
const std::type_info& sortedvector_typeinfo = typeid(android::SortedVectorImpl);
STUB
    clang++ -shared -fPIC -frtti -target aarch64-linux-android34 -o $OUT/stub.so $ROOT/include/stub.cpp
    rm $ROOT/include/stub.cpp

    echo "[*] Building libstratum.so..."
    clang++ $FLAGS $INCLUDES -shared -fPIC $ROOT/src/main.cpp $LIBS -o $OUT/libstratum.so

    echo "[*] Building default binary..."
    clang++ $FLAGS $INCLUDES $ROOT/src/default.cpp $LIBS -lstratum -o $ROOT/stratum-boot/system/bin/stratum_binary
    chmod +x $ROOT/stratum-boot/system/bin/stratum_binary
fi

if [[ $BUILD_EXAMPLES -eq 1 ]]; then
    if [[ ${#ONLY[@]} -gt 0 ]]; then
        targets=()
        for name in "${ONLY[@]}"; do
            # allow direct path to .cpp file
            if [[ $name == *.cpp ]]; then
                if [[ ! -f $name ]]; then
                    echo "[!] File not found: $name"
                    exit 1
                fi
                targets+=("$name")
            else
                f=""
                [[ -f $ROOT/apps/utils/$name.cpp ]] && f=$ROOT/apps/utils/$name.cpp
                [[ -f $ROOT/apps/demos/$name.cpp ]] && f=$ROOT/apps/demos/$name.cpp
                if [[ -z $f ]]; then
                    echo "[!] App not found: $name"
                    exit 1
                fi
                targets+=("$f")
            fi
        done
    else
        targets=($ROOT/apps/utils/*.cpp $ROOT/apps/demos/*.cpp)
    fi

    for f in "${targets[@]}"; do
        name=$(basename $f .cpp)
        echo "[*] Building: $name..."
        clang++ $FLAGS $INCLUDES -L$OUT $f $LIBS -lstratum -o $OUT/$name
    done
fi

echo "[*] Done! Run with: bash scripts/run_example.sh <name>"
