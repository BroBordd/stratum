#include "Stratum.h"
#include "StratumConfig.h"
#include <gui/SurfaceComposerClient.h>
#include <gui/Surface.h>
#include <ui/DisplayMode.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <linux/input.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <time.h>
#include <stdarg.h>
#include <android/log.h>
#include <thread>
#include <vector>
#include <functional>
#include <string.h>
#include <stdio.h>

using namespace android;
using namespace android::ui;

static float mono_now() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9f;
}

struct InputDev {
    int  fd;
    bool hasKeys;
    bool hasTouch;
    int  absXmin, absXmax;
    int  absYmin, absYmax;
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
        ioctl(dev.fd, EVIOCGABS(ABS_MT_POSITION_X), &ai);
        dev.absXmin = ai.minimum; dev.absXmax = ai.maximum;
        ioctl(dev.fd, EVIOCGABS(ABS_MT_POSITION_Y), &ai);
        dev.absYmin = ai.minimum; dev.absYmax = ai.maximum;
    }
}

static std::vector<InputDev> open_input_devices() {
    std::vector<InputDev> devs;

    {
        int fd = open_dev(StratumConfig::TOUCH_DEVICE);
        if (fd >= 0) {
            InputDev dev{};
            dev.fd       = fd;
            dev.hasKeys  = false;
            dev.hasTouch = true;
            fill_touch_ranges(dev);
            devs.push_back(dev);
        }
    }

    for (const char* path : {StratumConfig::KEY_DEVICE, StratumConfig::KEY_DEVICE2}) {
        if (!path || path[0] == '\0') continue;
        int fd = open_dev(path);
        if (fd >= 0) {
            InputDev dev{};
            dev.fd       = fd;
            dev.hasKeys  = true;
            dev.hasTouch = false;
            devs.push_back(dev);
        }
    }

    return devs;
}

struct ProtoAState {
    int  x          = 0;
    int  y          = 0;
    int  trackingId = -1;
    bool down       = false;
};

struct Slot {
    int  id     = -1;
    int  x      = 0;
    int  y      = 0;
    bool active = false;
};

struct Stratum::Impl {
    sp<SurfaceComposerClient> session;
    sp<SurfaceControl>        ctrl;
    sp<Surface>               surf;
    EGLDisplay dpy;
    EGLSurface esurf;
    EGLContext ctx;
    int w = 0, h = 0;

    bool running = true;

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
        std::vector<ProtoAState>        protoA(inputDevs.size());
        std::vector<std::vector<Slot>>  protoB(inputDevs.size(),
                                               std::vector<Slot>(StratumConfig::TOUCH_SLOTS));
        std::vector<int>                curSlot(inputDevs.size(), 0);

        while (running) {
            std::vector<pollfd> pfds;
            pfds.reserve(inputDevs.size());
            for (auto& dev : inputDevs)
                pfds.push_back({dev.fd, POLLIN, 0});

            poll(pfds.data(), pfds.size(), 16);

            for (size_t i = 0; i < inputDevs.size(); i++) {
                if (!(pfds[i].revents & POLLIN)) continue;
                auto& dev = inputDevs[i];
                struct input_event ev;

                while (read(dev.fd, &ev, sizeof(ev)) == sizeof(ev)) {
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
                                if (ev.value >= 0) {
                                    sl.id = ev.value; sl.active = true;
                                    float nx = (float)(sl.x - dev.absXmin) / (dev.absXmax - dev.absXmin);
                                    float ny = (float)(sl.y - dev.absYmin) / (dev.absYmax - dev.absYmin);
                                    if (touchCb) touchCb({s, sl.id, TouchAction::DOWN, nx, ny, t});
                                } else {
                                    float nx = (float)(sl.x - dev.absXmin) / (dev.absXmax - dev.absXmin);
                                    float ny = (float)(sl.y - dev.absYmin) / (dev.absYmax - dev.absYmin);
                                    if (touchCb) touchCb({s, sl.id, TouchAction::UP, nx, ny, t});
                                    sl.active = false; sl.id = -1;
                                }
                            } else if (ev.code == ABS_MT_POSITION_X) protoB[i][curSlot[i]].x = ev.value;
                            else if (ev.code == ABS_MT_POSITION_Y)   protoB[i][curSlot[i]].y = ev.value;
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

Stratum::Stratum() : mImpl(new Impl()) {}
Stratum::~Stratum() { delete mImpl; }

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
        EGL_DEPTH_SIZE, 0,
        EGL_NONE
    };
    EGLint n; EGLConfig cfg;
    eglChooseConfig(mImpl->dpy, attribs, &cfg, 1, &n);
    if (n == 0) return false;

    mImpl->esurf = eglCreateWindowSurface(mImpl->dpy, cfg, mImpl->surf.get(), nullptr);
    EGLint ctxAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    mImpl->ctx = eglCreateContext(mImpl->dpy, cfg, nullptr, ctxAttribs);
    eglMakeCurrent(mImpl->dpy, mImpl->esurf, mImpl->esurf, mImpl->ctx);

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
    mImpl->inputThread = std::thread([this]{ mImpl->inputLoop(); });

    const float TARGET = 1.0f / 60.0f;

    struct timespec start, now;
    clock_gettime(CLOCK_MONOTONIC, &start);

    while (mImpl->running) {
        float t0 = mono_now();

        clock_gettime(CLOCK_MONOTONIC, &now);
        float t = (now.tv_sec - start.tv_sec) +
                  (now.tv_nsec - start.tv_nsec) / 1e9f;

        if (mImpl->frameCb) mImpl->frameCb(t);
        eglSwapBuffers(mImpl->dpy, mImpl->esurf);

        float elapsed   = mono_now() - t0;
        float remaining = TARGET - elapsed;
        if (remaining > 0.0f)
            usleep((useconds_t)(remaining * 1e6f));
    }

    mImpl->inputThread.join();
}
