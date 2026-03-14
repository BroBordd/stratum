# Stratum

Native Android overlay framework. Fullscreen OpenGL ES 2.0 surface, raw input, ~60fps render loop. No Activity, no framework, no JVM.

Talks directly to SurfaceFlinger via `SurfaceComposerClient`, creates a surface at `INT_MAX` layer, reads input from `/dev/input/` event nodes in a dedicated thread.

---

## API

```cpp
Stratum s;
s.init();
s.onFrame([](float t) { /* t = seconds since run() */ });
s.onKey([](const KeyEvent& e) { /* e.code, e.action, e.time */ });
s.onTouch([](const TouchEvent& e) { /* e.x, e.y, normalised 0..1 */ });
s.run();   // blocks
s.stop();  // call from any callback

s.width();    // px
s.height();   // px
s.aspect();   // width / height

Stratum::vibrate(ms);
Stratum::setBrightness(0..255);
Stratum::log(tag, fmt, ...);
```

### KeyEvent

| Field | Type | Notes |
|---|---|---|
| `code` | `int` | Linux keycode (`KEY_POWER`, `KEY_VOLUMEUP`, ...) |
| `action` | `KeyAction` | `DOWN`, `UP`, `REPEAT` |
| `time` | `float` | `CLOCK_MONOTONIC` seconds |

Software repeat fires after `0.5s`, then every `~33ms`.

### TouchEvent

| Field | Type | Notes |
|---|---|---|
| `slot` | `int` | Multitouch slot |
| `id` | `int` | Tracking ID |
| `action` | `TouchAction` | `DOWN`, `MOVE`, `UP` |
| `x`, `y` | `float` | Normalised `0.0..1.0` |
| `time` | `float` | `CLOCK_MONOTONIC` seconds |

---

## Configuration

`include/StratumConfig.h` — set device paths and touch parameters for your target:

```cpp
namespace StratumConfig {
    const char* TOUCH_DEVICE   = "/dev/input/eventX";
    const char* KEY_DEVICE     = "/dev/input/eventY";
    const char* KEY_DEVICE2    = "";        // optional
    int  TOUCH_PROTOCOL        = 0;         // 0 = auto, 1 = proto A, 2 = proto B
    int  TOUCH_SLOTS           = 10;
    int  TOUCH_X_MIN = 0, TOUCH_X_MAX = 0; // 0 = ioctl auto-detect
    int  TOUCH_Y_MIN = 0, TOUCH_Y_MAX = 0;
    const char* VIBRATOR       = "/sys/class/timed_output/vibrator/enable";
    const char* BRIGHTNESS     = "/sys/class/leds/lcd-backlight/brightness";
    int  BRIGHTNESS_MIN = 0, BRIGHTNESS_MAX = 255;
}
```

---

## Helpers

**StratumText.h** — 8x8 bitmap font, GL texture atlas, no external files.

```cpp
Text::init(s.aspect());
Text::draw(str, x, y, size, r, g, b);
Text::drawWrapped(str, x, y, size, maxW, r, g, b);
float cw = Text::charW(size, aspect);
```

**StratumArgs.h** — timeout argument parser.

```cpp
float t = parseTimeout(argc, argv);
// accepts: N, -t N, -t=N, --timeout=N, --help
```

---

## Examples

| File | Description |
|---|---|
| `examples/bump.cpp` | Physics bounce demo |
| `examples/magma.cpp` | Lava lamp shader |
| `examples/menu.cpp` | Basic selection menu |
| `examples/paint.cpp` | Touch drawing canvas |
| `examples/piano.cpp` | Touch piano keys |
| `examples/signal.cpp` | Signal/waveform visualiser |

Run with:

```bash
scripts/run_example.sh <name>
```

---

## KernelSU Module

`stratum-boot/` is a KSU module template. It installs the runtime to `/system/bin` and `/system/lib64` and launches your binary early via `post-fs-data.sh`, before the Android framework starts. Replace `stratum_binary` with your executable.

```
stratum-boot/
├── module.prop
├── post-fs-data.sh
└── system/
    ├── bin/stratum, stratum_binary
    └── lib64/libstratum.so, stub.so
```

Build with `scripts/build_module.sh`.

---

## Building

Requires AOSP tree or a vendor NDK exposing private framework libraries.

```
LOCAL_SHARED_LIBRARIES := libgui libui libEGL libGLESv2 liblog libutils
LOCAL_CPPFLAGS         := -std=c++17
```

The binary must run as root or hold `CAP_SYS_ADMIN` to access input devices and SurfaceFlinger.

---

## License

Copyright (C) 2026 BrotherBoard — GNU General Public License v3.0. See [LICENSE](LICENSE).
