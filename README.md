# Stratum

A persistent SurfaceFlinger overlay layer for Android.

Stratum gives you a fullscreen hardware-composited surface sitting directly on top of SurfaceFlinger at maximum layer priority. It handles all the boilerplate — SurfaceComposerClient, EGL init, render loop — so you just write a frame callback and draw.

Built on a rooted Galaxy A14 (Android 14, SDK 34) after 6 hours of NDK hell. GPL-3.0 — use it anywhere, just mention the pain.

## Requirements

- Rooted Android device (Android 14 / SDK 34)
- Termux with clang (`pkg install clang`)
- AOSP vendor headers for your device (see Setup)

## Project Structure

```
stratum/
├── include/
│   ├── Stratum.h        # public API
│   └── compat.h         # bionic compatibility shims
├── src/
│   └── main.cpp         # Stratum core implementation
├── examples/
│   ├── gradient.cpp     # animated HSV gradient top-left to bottom-right
│   ├── pulse.cpp        # full screen color pulse
│   ├── plasma.cpp       # classic demoscene plasma
│   ├── ripple.cpp       # concentric rings from center
│   ├── tunnel.cpp       # infinite flying tunnel
│   ├── galaxy.cpp       # rotating spiral galaxy with stars
│   ├── lava.cpp         # lava lamp blobs
│   ├── aurora.cpp       # northern lights curtains
│   └── glitch.cpp       # digital glitch with scanlines
├── scripts/
│   ├── build.sh         # builds libstratum.so + all examples
│   ├── run_example.sh   # runs a built example on device
│   └── setup_headers.sh # instructions for setting up vendor headers
└── bin/                 # build output (gitignored)
```

## Setup

You need AOSP vendor headers matching your device. Place them under ```include/```:

```
include/
├── v34/arm64/    # AOSP Android 14 arm64 headers
├── logging/      # liblog headers
└── native/       # native framework headers
```

See `scripts/setup_headers.sh` for guidance.

## Build

```
bash scripts/build.sh
```

This produces:
- `bin/libstratum.so` — the Stratum shared library
- `bin/stub.so` — typeinfo stub for stripped system symbols
- `bin/<example>` — one binary per example

## Run

```
bash scripts/run_example.sh <example>
```

Examples: `gradient`, `pulse`, `plasma`, `ripple`, `tunnel`, `galaxy`, `lava`, `aurora`, `glitch`

```
bash scripts/run_example.sh plasma
```

## API

``` cpp
#include "Stratum.h"
#include <GLES2/gl2.h>

int main() {
    Stratum s;
    if (!s.init()) return 1;

    // setup your GL shaders here

    s.onFrame([&](float t) {
        // t = real elapsed seconds since start
        if (t > 5.0f) { s.stop(); return; }
        // draw your GL here
    });

    s.run();
    return 0;
}
```

## How It Works

Stratum creates a SurfaceComposerClient session and allocates a fullscreen SurfaceControl at INT_MAX layer priority, placing it above everything including the system UI. EGL is initialized against the surface and a render loop drives your frame callback at ~60fps using real elapsed time from CLOCK_MONOTONIC.

A stub shared library (`stub.so`) provides missing typeinfo symbols that are stripped from Android system libs but required at link time.

## License

GPL-3.0 — anyone can use, modify, and distribute, but must keep the license and credit the author.
