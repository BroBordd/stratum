#include "Stratum.h"
#include <gui/SurfaceComposerClient.h>
#include <gui/Surface.h>
#include <ui/DisplayMode.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <unistd.h>
#include <time.h>
#include <functional>

using namespace android;
using namespace android::ui;

struct Stratum::Impl {
    sp<SurfaceComposerClient> session;
    sp<SurfaceControl> ctrl;
    sp<Surface> surf;
    EGLDisplay dpy;
    EGLSurface esurf;
    EGLContext ctx;
    int w, h;
    bool running = true;
    std::function<void(float)> frameCb;
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
    EGLint n;
    EGLConfig cfg;
    eglChooseConfig(mImpl->dpy, attribs, &cfg, 1, &n);
    if (n == 0) return false;

    mImpl->esurf = eglCreateWindowSurface(mImpl->dpy, cfg, mImpl->surf.get(), nullptr);
    EGLint ctxAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    mImpl->ctx = eglCreateContext(mImpl->dpy, cfg, nullptr, ctxAttribs);
    eglMakeCurrent(mImpl->dpy, mImpl->esurf, mImpl->esurf, mImpl->ctx);

    return true;
}

void Stratum::onFrame(std::function<void(float t)> cb) {
    mImpl->frameCb = cb;
}

void Stratum::stop() {
    mImpl->running = false;
}

void Stratum::run() {
    struct timespec start, now;
    clock_gettime(CLOCK_MONOTONIC, &start);

    while (mImpl->running) {
        clock_gettime(CLOCK_MONOTONIC, &now);
        float t = (now.tv_sec - start.tv_sec) +
                  (now.tv_nsec - start.tv_nsec) / 1e9f;

        if (mImpl->frameCb) mImpl->frameCb(t);
        eglSwapBuffers(mImpl->dpy, mImpl->esurf);
        usleep(16667);
    }
}
