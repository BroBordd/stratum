# Stratum

A persistent SurfaceFlinger overlay layer for Android.

Holds a hardware-composited surface layer directly through SurfaceFlinger,
bypassing the normal Android window manager. Useful as a base for custom
boot animations, overlays, and display tools.

## Requirements
- Rooted Android device (Android 14 / SDK 34)
- Termux with clang installed (`pkg install clang`)

## Setup
```
bash scripts/setup_headers.sh
bash scripts/build.sh
bash scripts/run.sh
```

## How it works
Stratum creates a SurfaceComposerClient session, allocates a fullscreen
SurfaceControl at max layer priority, and renders to it via EGL/GLES2.
A stub shared library patches missing typeinfo symbols stripped from
system libs at build time.

## License
GPL-3.0 — use it anywhere, just mention the pain.
