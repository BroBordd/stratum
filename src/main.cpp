#include "Stratum.h"
#include "StratumConfig.h"
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <atomic>
#include <climits>
#include <fcntl.h>
#include <functional>
#include <linux/input.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <thread>
#include <time.h>
#include <unistd.h>
#include <vector>
#include <android/log.h>
#include <gui/Surface.h>
#include <gui/SurfaceComposerClient.h>
#include <ui/DisplayMode.h>

using namespace android;
using namespace android::ui;

struct ScopedFd {
    int fd = -1;
    ScopedFd() = default;
    explicit ScopedFd(int f) : fd(f) {}
    ~ScopedFd() { if (fd >= 0) close(fd); }
    ScopedFd(const ScopedFd&) = delete;
    ScopedFd& operator=(const ScopedFd&) = delete;
    ScopedFd(ScopedFd&& o) noexcept : fd(o.fd) { o.fd = -1; }
    ScopedFd& operator=(ScopedFd&& o) noexcept {
        if (this != &o) { if (fd >= 0) close(fd); fd = o.fd; o.fd = -1; }
        return *this;
    }
    int get() const { return fd; }
};

static float mono_now() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9f;
}

struct InputDev {
    ScopedFd fd;
    bool hasKeys  = false;
    bool hasTouch = false;
    int absXmin = 0, absXmax = 0;
    int absYmin = 0, absYmax = 0;
};

static int open_dev(const char* path) {
    return open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
}

static void fill_touch_ranges(InputDev& dev) {
    if (StratumConfig::TOUCH_X_MAX > 0) {
        dev.absXmin = StratumConfig::TOUCH_X_MIN;
        dev.absXmax = StratumConfig::TOUCH_X_MAX;
        dev.absYmin = StratumConfig::TOUCH_Y_MIN;
        dev.absYmax = StratumConfig::TOUCH_Y_MAX;
    } else {
        struct input_absinfo ai;
        ioctl(dev.fd.get(), EVIOCGABS(ABS_MT_POSITION_X), &ai);
        dev.absXmin = ai.minimum; dev.absXmax = ai.maximum;
        ioctl(dev.fd.get(), EVIOCGABS(ABS_MT_POSITION_Y), &ai);
        dev.absYmin = ai.minimum; dev.absYmax = ai.maximum;
    }
}

static std::vector<InputDev> open_input_devices() {
    std::vector<InputDev> devs;
    {
        int fd = open_dev(StratumConfig::TOUCH_DEVICE);
        if (fd >= 0) {
            InputDev dev;
            dev.fd       = ScopedFd(fd);
            dev.hasTouch = true;
            fill_touch_ranges(dev);
            devs.emplace_back(std::move(dev));
        }
    }
    for (const char* path : {StratumConfig::KEY_DEVICE, StratumConfig::KEY_DEVICE2}) {
        if (!path || path[0] == '\0') continue;
        int fd = open_dev(path);
        if (fd >= 0) {
            InputDev dev;
            dev.fd      = ScopedFd(fd);
            dev.hasKeys = true;
            devs.emplace_back(std::move(dev));
        }
    }
    return devs;
}

struct ProtoAState {
    int  x = 0, y = 0, trackingId = -1;
    bool down = false;
};

struct Slot {
    int  id = -1, x = 0, y = 0;
    bool active = false;
};

struct Stratum::Impl {
    sp<SurfaceComposerClient> session;
    sp<SurfaceControl>        ctrl;
    sp<Surface>               surf;
    EGLDisplay dpy   = EGL_NO_DISPLAY;
    EGLSurface esurf = EGL_NO_SURFACE;
    EGLContext ctx   = EGL_NO_CONTEXT;
    int w = 0, h = 0;

    std::atomic_bool running{true};

    std::function<void(float)>             frameCb;
    std::function<void(const KeyEvent&)>   keyCb;
    std::function<void(const TouchEvent&)> touchCb;

    std::vector<InputDev> inputDevs;
    std::thread           inputThread;

    int   repeatCode     = -1;
    float repeatNext     = 0;
    float repeatDelay    = 0.5f;
    float repeatInterval = 0.033f;

    void dispatchTouch(InputDev& dev, ProtoAState& ts, float t) {
        float nx = (float)(ts.x - dev.absXmin) / (dev.absXmax - dev.absXmin);
        float ny = (float)(ts.y - dev.absYmin) / (dev.absYmax - dev.absYmin);
        if (ts.trackingId >= 0 && !ts.down) {
            ts.down = true;
            if (touchCb) touchCb({0, ts.trackingId, TouchAction::DOWN, nx, ny, t});
        } else if (ts.trackingId == -1 && ts.down) {
            ts.down = false;
            if (touchCb) touchCb({0, 0, TouchAction::UP, nx, ny, t});
        } else if (ts.down) {
            if (touchCb) touchCb({0, ts.trackingId, TouchAction::MOVE, nx, ny, t});
        }
    }

    void dispatchTouchB(InputDev& dev, std::vector<Slot>& slots, float t) {
        for (int s = 0; s < StratumConfig::TOUCH_SLOTS; s++) {
            auto& sl = slots[s];
            if (!sl.active || sl.id < 0) continue;
            float nx = (float)(sl.x - dev.absXmin) / (dev.absXmax - dev.absXmin);
            float ny = (float)(sl.y - dev.absYmin) / (dev.absYmax - dev.absYmin);
            if (touchCb) touchCb({s, sl.id, TouchAction::MOVE, nx, ny, t});
        }
    }

    void inputLoop() {
        std::vector<ProtoAState>       protoA(inputDevs.size());
        std::vector<std::vector<Slot>> protoB(inputDevs.size(),
                                              std::vector<Slot>(StratumConfig::TOUCH_SLOTS));
        std::vector<int>               curSlot(inputDevs.size(), 0);
        std::vector<pollfd>            pfds;
        pfds.reserve(inputDevs.size());

        while (running) {
            pfds.clear();
            for (auto& dev : inputDevs)
                pfds.push_back({dev.fd.get(), POLLIN, 0});

            poll(pfds.data(), pfds.size(), 16);

            for (size_t i = 0; i < inputDevs.size(); i++) {
                if (!(pfds[i].revents & POLLIN)) continue;
                auto& dev = inputDevs[i];
                struct input_event ev;

                while (read(dev.fd.get(), &ev, sizeof(ev)) == sizeof(ev)) {
                    float t = mono_now();

                    if (ev.type == EV_KEY && dev.hasKeys && keyCb) {
                        if (ev.value == 1) {
                            keyCb({ev.code, KeyAction::DOWN, t});
                            repeatCode = ev.code;
                            repeatNext = t + repeatDelay;
                        } else if (ev.value == 0) {
                            keyCb({ev.code, KeyAction::UP, t});
                            if (repeatCode == ev.code) repeatCode = -1;
                        }
                    }

                    if (!dev.hasTouch) continue;
                    int proto = StratumConfig::TOUCH_PROTOCOL;

                    if (proto == 1) {
                        auto& ts = protoA[i];
                        if (ev.type == EV_ABS) {
                            if      (ev.code == ABS_MT_TRACKING_ID) ts.trackingId = ev.value;
                            else if (ev.code == ABS_MT_POSITION_X)  ts.x = ev.value;
                            else if (ev.code == ABS_MT_POSITION_Y)  ts.y = ev.value;
                        }
                        if (ev.type == EV_SYN && ev.code == SYN_REPORT)
                            dispatchTouch(dev, ts, t);

                    } else if (proto == 2) {
                        int s = curSlot[i];
                        if (ev.type == EV_ABS) {
                            if (ev.code == ABS_MT_SLOT) {
                                curSlot[i] = ev.value < StratumConfig::TOUCH_SLOTS ? ev.value : 0;
                            } else if (ev.code == ABS_MT_TRACKING_ID) {
                                auto& sl = protoB[i][s];
                                float nx = (float)(sl.x - dev.absXmin) / (dev.absXmax - dev.absXmin);
                                float ny = (float)(sl.y - dev.absYmin) / (dev.absYmax - dev.absYmin);
                                if (ev.value >= 0) {
                                    sl.id = ev.value; sl.active = true;
                                    if (touchCb) touchCb({s, sl.id, TouchAction::DOWN, nx, ny, t});
                                } else {
                                    if (touchCb) touchCb({s, sl.id, TouchAction::UP, nx, ny, t});
                                    sl.active = false; sl.id = -1;
                                }
                            } else if (ev.code == ABS_MT_POSITION_X) protoB[i][curSlot[i]].x = ev.value;
                            else if   (ev.code == ABS_MT_POSITION_Y) protoB[i][curSlot[i]].y = ev.value;
                        }
                        if (ev.type == EV_SYN && ev.code == SYN_REPORT)
                            dispatchTouchB(dev, protoB[i], t);

                    } else {
                        auto& ts = protoA[i];
                        if (ev.type == EV_ABS && ev.code == ABS_MT_SLOT) {
                            curSlot[i] = ev.value < StratumConfig::TOUCH_SLOTS ? ev.value : 0;
                            dev.hasTouch = true;
                        } else if (ev.type == EV_ABS) {
                            if      (ev.code == ABS_MT_TRACKING_ID) ts.trackingId = ev.value;
                            else if (ev.code == ABS_MT_POSITION_X)  ts.x = ev.value;
                            else if (ev.code == ABS_MT_POSITION_Y)  ts.y = ev.value;
                        }
                        if (ev.type == EV_SYN && ev.code == SYN_REPORT)
                            dispatchTouch(dev, ts, t);
                    }
                }
            }

            if (repeatCode >= 0 && keyCb) {
                float t = mono_now();
                if (t >= repeatNext) {
                    keyCb({repeatCode, KeyAction::REPEAT, t});
                    repeatNext = t + repeatInterval;
                }
            }
        }
    }
};

Stratum::Stratum() : mImpl(std::make_unique<Impl>()) {}

Stratum::~Stratum() {
    mImpl->running = false;
    if (mImpl->inputThread.joinable())
        mImpl->inputThread.join();
    if (mImpl->dpy != EGL_NO_DISPLAY) {
        eglMakeCurrent(mImpl->dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (mImpl->ctx   != EGL_NO_CONTEXT) eglDestroyContext(mImpl->dpy, mImpl->ctx);
        if (mImpl->esurf != EGL_NO_SURFACE) eglDestroySurface(mImpl->dpy, mImpl->esurf);
        eglTerminate(mImpl->dpy);
    }
    mImpl->surf.clear();
    mImpl->ctrl.clear();
    mImpl->session.clear();
}

bool Stratum::init() {
    mImpl->session = new SurfaceComposerClient();
    if (mImpl->session->initCheck() != 0) return false;

    const auto ids = SurfaceComposerClient::getPhysicalDisplayIds();
    if (ids.empty()) return false;

    sp<IBinder> token = SurfaceComposerClient::getPhysicalDisplayToken(ids.front());
    ui::DisplayMode mode;
    if (SurfaceComposerClient::getActiveDisplayMode(token, &mode) != 0) return false;

    mImpl->w = mode.resolution.getWidth();
    mImpl->h = mode.resolution.getHeight();

    mImpl->ctrl = mImpl->session->createSurface(
        String8("stratum"), mImpl->w, mImpl->h,
        PIXEL_FORMAT_RGBA_8888, ISurfaceComposerClient::eOpaque);
    if (!mImpl->ctrl || !mImpl->ctrl->isValid()) return false;

    SurfaceComposerClient::Transaction()
        .setLayer(mImpl->ctrl, INT_MAX)
        .setPosition(mImpl->ctrl, 0, 0)
        .setSize(mImpl->ctrl, mImpl->w, mImpl->h)
        .show(mImpl->ctrl)
        .apply(true);

    mImpl->surf = mImpl->ctrl->getSurface();

    mImpl->dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(mImpl->dpy, nullptr, nullptr);

    const EGLint attribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE,   8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE,  8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 0,
        EGL_NONE
    };
    EGLint n; EGLConfig cfg;
    eglChooseConfig(mImpl->dpy, attribs, &cfg, 1, &n);
    if (n == 0) return false;

    mImpl->esurf = eglCreateWindowSurface(mImpl->dpy, cfg, mImpl->surf.get(), nullptr);
    EGLint ctxAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    mImpl->ctx = eglCreateContext(mImpl->dpy, cfg, nullptr, ctxAttribs);
    eglMakeCurrent(mImpl->dpy, mImpl->esurf, mImpl->esurf, mImpl->ctx);
    eglSwapInterval(mImpl->dpy, 1);

    mImpl->inputDevs = open_input_devices();
    Stratum::log("stratum", "init() done, %zu input devices", mImpl->inputDevs.size());
    return true;
}

void Stratum::onFrame(std::function<void(float t)> cb)           { mImpl->frameCb  = cb; }
void Stratum::onKey(std::function<void(const KeyEvent&)> cb)     { mImpl->keyCb    = cb; }
void Stratum::onTouch(std::function<void(const TouchEvent&)> cb) { mImpl->touchCb  = cb; }
void Stratum::stop()    { mImpl->running = false; }
int   Stratum::width()  const { return mImpl->w; }
int   Stratum::height() const { return mImpl->h; }
float Stratum::aspect() const { return (float)mImpl->w / (float)mImpl->h; }

void Stratum::vibrate(int ms) {
    FILE* f = fopen(StratumConfig::VIBRATOR, "w");
    if (f) { fprintf(f, "%d\n", ms); fclose(f); }
}

void Stratum::setBrightness(int val) {
    if (val < StratumConfig::BRIGHTNESS_MIN) val = StratumConfig::BRIGHTNESS_MIN;
    if (val > StratumConfig::BRIGHTNESS_MAX) val = StratumConfig::BRIGHTNESS_MAX;
    FILE* f = fopen(StratumConfig::BRIGHTNESS, "w");
    if (f) { fprintf(f, "%d\n", val); fclose(f); }
}

void Stratum::log(const char* tag, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    __android_log_vprint(ANDROID_LOG_DEBUG, tag, fmt, args);
    va_end(args);
}

void Stratum::run() {
    Stratum::log("stratum", "run() started, w=%d h=%d", mImpl->w, mImpl->h);
    mImpl->inputThread = std::thread([this] { mImpl->inputLoop(); });

    struct timespec start, now;
    clock_gettime(CLOCK_MONOTONIC, &start);

    while (mImpl->running) {
        clock_gettime(CLOCK_MONOTONIC, &now);
        float t = (now.tv_sec  - start.tv_sec) +
                  (now.tv_nsec - start.tv_nsec) / 1e9f;
        if (mImpl->frameCb) mImpl->frameCb(t);
        eglSwapBuffers(mImpl->dpy, mImpl->esurf);
    }

    mImpl->inputThread.join();
}
