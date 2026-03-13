#include <unistd.h>
#include <stdio.h>
#include <gui/SurfaceComposerClient.h>
#include <gui/Surface.h>
#include <ui/DisplayMode.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

using namespace android;
using namespace android::ui;

int main() {
    fprintf(stderr, "[1] starting\n");

    sp<SurfaceComposerClient> session = new SurfaceComposerClient();
    fprintf(stderr, "[2] session created, initCheck=%d\n", session->initCheck());

    const auto ids = SurfaceComposerClient::getPhysicalDisplayIds();
    fprintf(stderr, "[3] display ids count=%zu\n", ids.size());
    if (ids.empty()) { fprintf(stderr, "ERROR: no displays\n"); return 1; }

    sp<IBinder> token = SurfaceComposerClient::getPhysicalDisplayToken(ids.front());
    fprintf(stderr, "[4] token=%p\n", token.get());

    ui::DisplayMode mode;
    status_t err = SurfaceComposerClient::getActiveDisplayMode(token, &mode);
    fprintf(stderr, "[5] getActiveDisplayMode err=%d w=%d h=%d\n", err, mode.resolution.getWidth(), mode.resolution.getHeight());

    int w = mode.resolution.getWidth();
    int h = mode.resolution.getHeight();
    if (w == 0 || h == 0) { fprintf(stderr, "ERROR: bad resolution\n"); return 1; }

    sp<SurfaceControl> ctrl = session->createSurface(
        String8("surfcap"), w, h, PIXEL_FORMAT_RGBA_8888,
        ISurfaceComposerClient::eOpaque);
    fprintf(stderr, "[6] surface control=%p\n", ctrl.get());
    if (!ctrl || !ctrl->isValid()) { fprintf(stderr, "ERROR: invalid surface control\n"); return 1; }

    status_t txErr = SurfaceComposerClient::Transaction()
        .setLayer(ctrl, INT_MAX)
        .setPosition(ctrl, 0, 0)
        .setSize(ctrl, w, h)
        .show(ctrl)
        .apply(true);
    fprintf(stderr, "[7] transaction err=%d\n", txErr);

    sp<Surface> surf = ctrl->getSurface();
    fprintf(stderr, "[8] surface=%p\n", surf.get());

    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    fprintf(stderr, "[9] eglDisplay=%p\n", dpy);
    EGLBoolean initOk = eglInitialize(dpy, nullptr, nullptr);
    fprintf(stderr, "[10] eglInitialize=%d err=0x%x\n", initOk, eglGetError());

    const EGLint attribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE,   8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE,  8,
        EGL_DEPTH_SIZE, 0,
        EGL_NONE
    };
    EGLint n = 0;
    EGLConfig cfg;
    eglChooseConfig(dpy, attribs, &cfg, 1, &n);
    fprintf(stderr, "[11] eglChooseConfig n=%d err=0x%x\n", n, eglGetError());

    EGLSurface esurf = eglCreateWindowSurface(dpy, cfg, surf.get(), nullptr);
    fprintf(stderr, "[12] eglSurface=%p err=0x%x\n", esurf, eglGetError());

    EGLint ctxAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext ctx = eglCreateContext(dpy, cfg, nullptr, ctxAttribs);
    fprintf(stderr, "[13] eglContext=%p err=0x%x\n", ctx, eglGetError());

    EGLBoolean makeOk = eglMakeCurrent(dpy, esurf, esurf, ctx);
    fprintf(stderr, "[14] eglMakeCurrent=%d err=0x%x\n", makeOk, eglGetError());

    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    fprintf(stderr, "[15] entering render loop\n");

    int frames = 0;
    while (true) {
        glClear(GL_COLOR_BUFFER_BIT);
        EGLBoolean swapOk = eglSwapBuffers(dpy, esurf);
        if (frames < 5) fprintf(stderr, "[frame %d] swapOk=%d err=0x%x\n", frames, swapOk, eglGetError());
        frames++;
        usleep(33333);
    }

    return 0;
}
