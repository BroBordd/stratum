# Stratum

A lightweight C++ framework for writing native Android overlay apps that run directly on top of SurfaceFlinger — no Activity, no framework, no Java. You get a fullscreen OpenGL ES 2.0 surface, hardware key input, multitouch, haptics, and a ~60fps render loop in a single header-driven API.

Built for tools that need to run early, run lean, and stay out of Android's way — boot menus, recovery overlays, diagnostics, kiosks.

## How it works

Stratum talks directly to **SurfaceFlinger** via `SurfaceComposerClient`, creates a surface at `INT_MAX` layer (above everything), and sets up an **EGL** context on it. Input is read from raw `/dev/input/` event nodes in a dedicated thread. The render loop runs at ~60fps (`usleep(16667)`) on the main thread, calling your `onFrame` callback each tick with elapsed time in seconds.

No Choreographer. No Looper. No JVM.

## API

```cpp
Stratum s;
s.init();               // set up surface + EGL + input devices
s.onFrame([](float t) { /* t = seconds since run() */ });
s.onKey([](const KeyEvent& e) { /* e.code, e.action, e.time */ });
s.onTouch([](const TouchEvent& e) { /* e.x, e.y normalised 0..1 */ });
s.run();                // blocks until stop() is called
s.stop();               // call from any callback to exit

s.width();              // display width in px
s.height();             // display height in px
s.aspect();             // width / height

Stratum::vibrate(ms);           // write to vibrator sysfs node
Stratum::setBrightness(0..255); // write to brightness sysfs node
Stratum::log(tag, fmt, ...);    // __android_log_print wrapper
```

### KeyEvent

| Field | Type | Notes |
|---|---|---|
| `code` | `int` | Linux input keycode (`KEY_POWER`, `KEY_VOLUMEUP`, ...) |
| `action` | `KeyAction` | `DOWN`, `UP`, `REPEAT` |
| `time` | `float` | `CLOCK_MONOTONIC` seconds |

Software key repeat is handled internally — `REPEAT` fires after `0.5s` delay then every `~33ms`.

### TouchEvent

| Field | Type | Notes |
|---|---|---|
| `slot` | `int` | Multitouch slot index |
| `id` | `int` | Tracking ID |
| `action` | `TouchAction` | `DOWN`, `MOVE`, `UP` |
| `x`, `y` | `float` | Normalised `0.0..1.0` |
| `time` | `float` | `CLOCK_MONOTONIC` seconds |

## Configuration

Device paths and touch parameters live in `StratumConfig.h`:

```cpp
namespace StratumConfig {
    const char* TOUCH_DEVICE  = "/dev/input/eventX";
    const char* KEY_DEVICE    = "/dev/input/eventY";
    const char* KEY_DEVICE2   = "";               // optional second key device
    int         TOUCH_PROTOCOL = 0;               // 0 = auto, 1 = proto A, 2 = proto B
    int         TOUCH_SLOTS    = 10;
    int         TOUCH_X_MIN = 0, TOUCH_X_MAX = 0; // 0 = auto-detect via ioctl
    int         TOUCH_Y_MIN = 0, TOUCH_Y_MAX = 0;
    const char* VIBRATOR      = "/sys/class/timed_output/vibrator/enable";
    const char* BRIGHTNESS    = "/sys/class/leds/lcd-backlight/brightness";
    int         BRIGHTNESS_MIN = 0, BRIGHTNESS_MAX = 255;
}
```

Touch coordinate ranges are auto-detected via `EVIOCGABS` ioctl if `TOUCH_X_MAX` is left at `0`. Touch protocol is auto-detected from the event stream if left at `0`.

## Helpers

### StratumText.h

An 8x8 bitmap font renderer backed by a GL texture atlas. No external font files.

```cpp
Text::init(s.aspect());   // call once after s.init()

Text::draw(str, x, y, size, r, g, b);          // x/y/size in normalised screen space
Text::drawWrapped(str, x, y, size, maxW, r, g, b);
float cw = Text::charW(size, aspect);           // character width helper
```

### StratumArgs.h

```cpp
float t = parseTimeout(argc, argv);
// accepts: positional number, -t N, -t=N, --timeout=N, --help
```

## Writing an app

```cpp
#include "Stratum.h"
#include "StratumText.h"

int main() {
    Stratum s;
    s.init();
    Text::init(s.aspect());

    s.onFrame([&](float t) {
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        Text::draw("HELLO", 0.1f, 0.1f, 0.06f, 1, 1, 1);
    });

    s.onKey([&](const KeyEvent& e) {
        if (e.code == KEY_POWER && e.action == KeyAction::DOWN)
            s.stop();
    });

    s.run();
}
```

## KernelSU Module

Stratum ships a ready-to-flash KernelSU module (`stratum-boot.zip`) that installs the binary and libraries to `/system/bin` and `/system/lib64`, and runs the boot menu automatically via `post-fs-data.sh` before the Android framework starts.

```
stratum-boot/
├── module.prop
├── post-fs-data.sh
└── system/
    ├── bin/stratum, stratum_binary
    └── lib64/libstratum.so, stub.so
```

Flash via KSU manager like any other module. No Magisk required.

## Building

Stratum uses private Android framework APIs (`libgui`, `libui`). It must be built inside the AOSP tree or with a vendor NDK that exposes these libraries.

Add to your `Android.mk` or `CMakeLists.txt`:

```
LOCAL_SHARED_LIBRARIES := libgui libui libEGL libGLESv2 liblog libutils
LOCAL_CPPFLAGS         := -std=c++17
```

The binary needs to run as root or with `CAP_SYS_ADMIN` to access input devices and SurfaceFlinger.

## License

Copyright (C) 2026 BrotherBoard

Licensed under the GNU General Public License v3.0. See [LICENSE](LICENSE) for details.
