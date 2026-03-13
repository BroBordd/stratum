#!/bin/bash
set -e

OUT=bin
mkdir -p $OUT

echo "[*] Building stub..."
cat > $TMPDIR/stub.cpp << 'STUB'
#include <typeinfo>
namespace android {
    class VectorImpl { public: virtual ~VectorImpl() {} };
    class SortedVectorImpl : public VectorImpl { public: virtual ~SortedVectorImpl() {} };
}
const std::type_info& sortedvector_typeinfo = typeid(android::SortedVectorImpl);
STUB
clang++ -shared -fPIC -frtti -target aarch64-linux-android34 -o $OUT/stub.so $TMPDIR/stub.cpp

INCLUDES="\
  -Iinclude \
  -Iinclude/v34/arm64/include/frameworks/native/libs/gui/include \
  -Iinclude/v34/arm64/include/frameworks/native/libs/binder/include \
  -Iinclude/v34/arm64/include/frameworks/native/libs/ui/include \
  -Iinclude/v34/arm64/include/frameworks/native/libs/math/include \
  -Iinclude/v34/arm64/include/frameworks/native/libs/nativewindow/include \
  -Iinclude/v34/arm64/include/frameworks/native/libs/nativebase/include \
  -Iinclude/v34/arm64/include/frameworks/native/opengl/include \
  -Iinclude/v34/arm64/include/system/libbase/include \
  -Iinclude/v34/arm64/include/system/core/libutils/include \
  -Iinclude/v34/arm64/include/system/core/libcutils/include \
  -Iinclude/v34/arm64/include/system/core/libsystem/include \
  -Iinclude/v34/arm64/include/system/libhidl/base/include \
  -Iinclude/v34/arm64/include/system/libhidl/transport/token/1.0/utils/include \
  -Iinclude/v34/arm64/include/system/libfmq/base \
  -Iinclude/v34/arm64/include/hardware/libhardware/include \
  -Iinclude/v34/arm64/include/generated-headers/frameworks/native/libs/gui/libgui_aidl_static/android_vendor.34_arm64_armv8-a_static/gen/aidl \
  -Iinclude/v34/arm64/include/generated-headers/frameworks/native/libs/gui/libgui/android_vendor.34_arm64_armv8-a_shared/gen/aidl \
  -Iinclude/v34/arm64/include/generated-headers/frameworks/native/libs/gui/libgui_window_info_static/android_vendor.34_arm64_armv8-a_static_afdo-libgui_lto-thin/gen/aidl \
  -Iinclude/v34/arm64/include/generated-headers/hardware/interfaces/graphics/common/aidl/android.hardware.graphics.common-V4-ndk-source/gen/include \
  -Iinclude/v34/arm64/include/generated-headers/hardware/interfaces/graphics/common/1.0/android.hardware.graphics.common@1.0_genc++_headers/gen \
  -Iinclude/v34/arm64/include/generated-headers/hardware/interfaces/graphics/common/1.1/android.hardware.graphics.common@1.1_genc++_headers/gen \
  -Iinclude/v34/arm64/include/generated-headers/hardware/interfaces/graphics/common/1.2/android.hardware.graphics.common@1.2_genc++_headers/gen \
  -Iinclude/v34/arm64/include/generated-headers/hardware/interfaces/graphics/bufferqueue/1.0/android.hardware.graphics.bufferqueue@1.0_genc++_headers/gen \
  -Iinclude/v34/arm64/include/generated-headers/hardware/interfaces/graphics/bufferqueue/2.0/android.hardware.graphics.bufferqueue@2.0_genc++_headers/gen \
  -Iinclude/v34/arm64/include/generated-headers/system/libhidl/transport/base/1.0/android.hidl.base@1.0_genc++_headers/gen \
  -Iinclude/v34/arm64/include/generated-headers/system/libhidl/transport/manager/1.0/android.hidl.manager@1.0_genc++_headers/gen \
  -Iinclude/v34/arm64/include/generated-headers/hardware/interfaces/media/1.0/android.hardware.media@1.0_genc++_headers/gen \
  -Iinclude/v34/arm64/include/system/libhwbinder/include \
  -Iinclude/logging/liblog/include"

FLAGS="-std=c++17 -target aarch64-linux-android34 -D__BIONIC__ -w -include $(dirname $0)/../include/compat.h"
LIBS="-L/system/lib64 -lgui -lui -lEGL -lGLESv2 -lbinder -lutils -llog -Wl,--allow-shlib-undefined -Wl,--unresolved-symbols=ignore-all"

TARGET=${1:-example}

if [ "$TARGET" = "example" ]; then
    echo "[*] Building stratum..."
    clang++ $FLAGS $INCLUDES src/main.cpp examples/gradient.cpp $LIBS -o $OUT/stratum
elif [ "$TARGET" = "all" ]; then
    echo "[*] Building stratum..."
    clang++ $FLAGS $INCLUDES src/main.cpp examples/gradient.cpp $LIBS -o $OUT/stratum
fi

echo "[*] Done! Binary at $OUT/stratum"
