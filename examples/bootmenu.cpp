#include "Stratum.h"
#include "StratumText.h"
#include "StratumArgs.h"
#include <GLES2/gl2.h>
#include <linux/input-event-codes.h>
#include <math.h>
#include <mutex>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <vector>
#include <string>

static float mono_now() {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9f;
}

static const char* VSH = R"(
attribute vec2 pos;
void main() { gl_Position = vec4(pos, 0.0, 1.0); }
)";

static const char* FSH = R"(
precision mediump float;
uniform vec4 color;
void main() { gl_FragColor = color; }
)";

static GLuint compileShader(GLenum type, const char* src) {
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, nullptr);
    glCompileShader(sh);
    return sh;
}

static GLint gPosLoc   = -1;
static GLint gColorLoc = -1;

static void drawRect(float x0, float y0, float x1, float y1,
                     float r, float g, float b, float a = 1.0f) {
    float nx0=x0*2-1, nx1=x1*2-1, ny0=1-y0*2, ny1=1-y1*2;
    float v[] = {nx0,ny0, nx1,ny0, nx0,ny1, nx1,ny1};
    glUniform4f(gColorLoc, r, g, b, a);
    glVertexAttribPointer(gPosLoc, 2, GL_FLOAT, GL_FALSE, 0, v);
    glEnableVertexAttribArray(gPosLoc);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

static void drawRoundRect(float x0, float y0, float x1, float y1,
                          float r, float g, float b, float a = 1.0f,
                          float rad = 0.012f) {
    drawRect(x0+rad, y0,     x1-rad, y1,     r,g,b,a);
    drawRect(x0,     y0+rad, x1,     y1-rad, r,g,b,a);
}

struct BootItem {
    const char* label;
    const char* icon;
    const char* cmd;
    const char* confirmMsg;
    float ar, ag, ab;
};

static const BootItem ITEMS[] = {
    { "CONTINUE TO SYSTEM", ">", nullptr,
      "CONTINUE BOOTING ANDROID?",
      0.25f, 0.85f, 0.45f },
    { "REBOOT", "R", "reboot",
      "REBOOT THE DEVICE?",
      1.00f, 0.00f, 1.00f },
    { "RECOVERY", "!", "reboot recovery",
      "REBOOT INTO RECOVERY?",
      1.00f, 0.70f, 0.20f },
    { "DOWNLOAD MODE", "D", "reboot download",
      "REBOOT INTO DOWNLOAD MODE?",
      0.20f, 0.55f, 1.00f },
    { "POWER OFF", "X", "reboot -p",
      "POWER OFF THE DEVICE?",
      1.00f, 0.30f, 0.30f },
    { "ADVANCED", "*", nullptr, nullptr,
      1.00f, 0.78f, 0.10f },
};
static const int ITEM_COUNT = 6;
static const int EXTRAS_IDX = 5;

static std::vector<std::string> gExtras;
static const char* EXTRAS_DIR = getenv("EXTRAS_DIR") ? getenv("EXTRAS_DIR") : "/data/adb/modules/boot-menu/extras";

static void loadExtras() {
    gExtras.clear();
    DIR* d = opendir(EXTRAS_DIR);
    if (!d) return;
    struct dirent* e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        gExtras.push_back(e->d_name);
    }
    closedir(d);
}

enum class Screen { MENU, CONFIRM, EXTRAS };

static bool gDryRun = false;

static void execAction(int idx) {
    const BootItem& it = ITEMS[idx];
    if (gDryRun) {
        Stratum::log("bootmenu", "[DRY] would run: %s", it.cmd ? it.cmd : "(exit)");
        return;
    }
    if (it.cmd) {
        char buf[128];
        snprintf(buf, sizeof(buf), "/system/bin/sh -c '%s'", it.cmd);
        system(buf);
    }
}

static void execExtra(const std::string& name) {
    if (gDryRun) {
        Stratum::log("bootmenu", "[DRY] would launch extra: %s", name.c_str());
        return;
    }
    char buf[512];
    snprintf(buf, sizeof(buf),
        "/system/bin/sh -c '"
        "export LD_LIBRARY_PATH=/data/adb/modules/boot-menu/system/lib64:/system/lib64; "
        "export LD_PRELOAD=/data/adb/modules/boot-menu/system/lib64/stub.so; "
        "%s/%s --timeout=0'",
        EXTRAS_DIR, name.c_str());
    system(buf);
}

int main(int argc, char** argv) {
    float timeout = 0.0f;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--dry") == 0)
            gDryRun = true;
        else if (strncmp(argv[i], "--timeout=", 10) == 0)
            timeout = atof(argv[i] + 10);
        else if (strcmp(argv[i], "--timeout") == 0 && i+1 < argc)
            timeout = atof(argv[++i]);
    }

    loadExtras();

    Stratum s;
    if (!s.init()) return 1;
    Text::init(s.aspect());

    GLuint vs   = compileShader(GL_VERTEX_SHADER,   VSH);
    GLuint fs   = compileShader(GL_FRAGMENT_SHADER, FSH);
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs); glAttachShader(prog, fs);
    glLinkProgram(prog);
    glDetachShader(prog, vs); glDeleteShader(vs);
    glDetachShader(prog, fs); glDeleteShader(fs);
    glUseProgram(prog);

    gPosLoc   = glGetAttribLocation(prog,  "pos");
    gColorLoc = glGetUniformLocation(prog, "color");

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    std::mutex mtx;
    Screen screen     = Screen::MENU;
    int    menuSel    = 0;
    int    dlgSel     = 0;
    int    pendingIdx = -1;
    float  flashT     = -1.f;
    int    flashIdx   = -1;
    int    extrasSel  = 0;

    float ignoreInputUntil = 0.f;
    float startTime = mono_now();

    s.onKey([&](const KeyEvent& e) {
        if (e.action != KeyAction::DOWN && e.action != KeyAction::REPEAT) return;
        if (mono_now() < ignoreInputUntil) return;
        Stratum::vibrate(22);
        std::lock_guard<std::mutex> lk(mtx);
        if (screen == Screen::MENU) {
            if (e.code == KEY_VOLUMEUP)
                menuSel = (menuSel - 1 + ITEM_COUNT) % ITEM_COUNT;
            if (e.code == KEY_VOLUMEDOWN)
                menuSel = (menuSel + 1) % ITEM_COUNT;
            if (e.code == KEY_POWER) {
                if (menuSel == EXTRAS_IDX) { extrasSel = 0; screen = Screen::EXTRAS; }
                else { pendingIdx = menuSel; dlgSel = 0; screen = Screen::CONFIRM; }
            }
        } else if (screen == Screen::CONFIRM) {
            if (e.code == KEY_VOLUMEUP || e.code == KEY_VOLUMEDOWN) dlgSel ^= 1;
            if (e.code == KEY_POWER) {
                if (dlgSel == 0) { flashIdx = pendingIdx; flashT = mono_now(); }
                else             { screen = Screen::MENU; }
            }
        } else if (screen == Screen::EXTRAS) {
            int total = (int)gExtras.size() + 1;
            if (e.code == KEY_VOLUMEUP)   extrasSel = (extrasSel - 1 + total) % total;
            if (e.code == KEY_VOLUMEDOWN) extrasSel = (extrasSel + 1) % total;
            if (e.code == KEY_POWER) {
                if (extrasSel == 0) {
                    screen = Screen::MENU;
                } else {
                    int idx = extrasSel - 1;
                    execExtra(gExtras[idx]);
                    // return to menu, reset selection, long ignore window
                    screen   = Screen::MENU;
                    extrasSel = 0;
                    ignoreInputUntil = mono_now() + 1.5f;
                }
            }
        }
    });

    s.onTouch([&](const TouchEvent& e) {
        if (e.action != TouchAction::DOWN) return;
        if (mono_now() < ignoreInputUntil) return;
        Stratum::vibrate(28);
        std::lock_guard<std::mutex> lk(mtx);
        if (screen == Screen::MENU) {
            float itemH  = 0.095f;
            float startY = 0.175f;
            int tapped = (int)((e.y - startY) / itemH);
            if (tapped < 0 || tapped >= ITEM_COUNT) return;
            if (tapped == menuSel) {
                if (menuSel == EXTRAS_IDX) { extrasSel = 0; screen = Screen::EXTRAS; }
                else { pendingIdx = menuSel; dlgSel = 0; screen = Screen::CONFIRM; }
            } else {
                menuSel = tapped;
            }
        } else if (screen == Screen::CONFIRM) {
            float btnY0=0.555f, btnY1=0.640f;
            if (e.y < btnY0 || e.y > btnY1) return;
            bool hitYes = e.x >= 0.10f && e.x <= 0.44f;
            bool hitNo  = e.x >= 0.56f && e.x <= 0.90f;
            if (hitYes) {
                if (dlgSel == 0) { flashIdx = pendingIdx; flashT = mono_now(); }
                else             { dlgSel = 0; }
            } else if (hitNo) {
                if (dlgSel == 1) { screen = Screen::MENU; }
                else             { dlgSel = 1; }
            }
        } else if (screen == Screen::EXTRAS) {
            int cnt   = (int)gExtras.size();
            int total = cnt + 1;
            float itemH  = 0.085f;
            float startY = 0.175f;
            int tapped = (int)((e.y - startY) / itemH);
            if (tapped < 0 || tapped >= total) return;
            if (tapped == extrasSel) {
                if (tapped == 0) {
                    screen = Screen::MENU;
                } else {
                    int idx = tapped - 1;
                    execExtra(gExtras[idx]);
                    // return to menu, reset selection, long ignore window
                    screen   = Screen::MENU;
                    extrasSel = 0;
                    ignoreInputUntil = mono_now() + 1.5f;
                }
            } else {
                extrasSel = tapped;
            }
        }
    });

    s.onFrame([&](float t) {
        float now = mono_now();

        if (timeout > 0.0f && (now - startTime) >= timeout) {
            execAction(0);
            s.stop();
            return;
        }

        {
            std::lock_guard<std::mutex> lk(mtx);
            if (flashIdx >= 0 && (now - flashT) > 0.25f) {
                int idx  = flashIdx;
                flashIdx = -1;
                execAction(idx);
                s.stop();
                return;
            }
        }

        Screen curScreen; int curMenuSel, curDlgSel, curPending, curFlashIdx, curExtrasSel;
        float  curFlashT;
        {
            std::lock_guard<std::mutex> lk(mtx);
            curScreen    = screen;
            curMenuSel   = menuSel;
            curDlgSel    = dlgSel;
            curPending   = pendingIdx;
            curFlashIdx  = flashIdx;
            curFlashT    = flashT;
            curExtrasSel = extrasSel;
        }

        float asp = s.aspect();

        glClearColor(0.04f, 0.04f, 0.06f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(prog);

        if (gDryRun) {
            float ds  = fminf(0.020f, 0.18f * asp / 7);
            float ph  = ds + 0.012f;
            float pw  = 7 * ds / asp + 0.030f;
            float px0 = 1.0f - pw - 0.02f;
            float py0 = 0.012f;
            drawRoundRect(px0, py0, px0+pw, py0+ph, 0.45f, 0.22f, 0.02f, 0.92f, 0.008f);
            Text::draw("DRY RUN", px0+0.012f, py0+0.006f, ds, 1.0f, 0.75f, 0.20f);
        }

        drawRect(0.f, 0.f, 1.f, 0.004f, 0.25f, 0.50f, 1.0f, 0.9f);

        float hdrSize = fminf(0.055f, 0.60f * asp / 7);
        Text::draw("STRATUM", 0.05f, 0.04f, hdrSize, 0.40f, 0.70f, 1.00f);
        float subSize = fminf(0.028f, 0.60f * asp / 12);
        Text::draw("BOOT MENU", 0.05f, 0.04f + hdrSize + 0.005f, subSize, 0.22f, 0.35f, 0.55f);
        float verSize = fminf(0.016f, 0.60f * asp / 6);
        Text::draw("v1.0", 0.05f, 0.04f + hdrSize + 0.005f + subSize + 0.004f, verSize, 0.12f, 0.14f, 0.18f);
        drawRect(0.05f, 0.155f, 0.95f, 0.157f, 0.18f, 0.35f, 0.70f, 0.8f);

        // ── extras screen ─────────────────────────────────────────────────
        if (curScreen == Screen::EXTRAS) {
            float itemH  = 0.085f;
            float startY = 0.175f;
            int   cnt    = (int)gExtras.size();
            int   total  = cnt + 1;
            float textSize = fminf(0.030f, 0.75f * asp / 20);

            for (int i = 0; i < total; i++) {
                bool isSel  = (i == curExtrasSel);
                bool isBack = (i == 0);
                float y0 = startY + i * itemH;
                float y1 = y0 + itemH - 0.010f;

                if (isSel) {
                    float hr = isBack ? 0.20f : 0.18f;
                    float hg = isBack ? 0.20f : 0.14f;
                    float hb = isBack ? 0.22f : 0.02f;
                    float ar = isBack ? 0.55f : 1.00f;
                    float ag = isBack ? 0.55f : 0.78f;
                    float ab = isBack ? 0.65f : 0.10f;
                    drawRoundRect(0.03f, y0, 0.97f, y1, hr, hg, hb, 0.55f);
                    drawRoundRect(0.030f, y0, 0.038f, y1, ar, ag, ab, 1.0f, 0.004f);
                } else {
                    drawRoundRect(0.03f, y0, 0.97f, y1, 0.07f, 0.07f, 0.09f, 0.65f);
                }

                float tr = isSel ? 0.92f : 0.40f;
                float tg = isSel ? 0.96f : 0.43f;
                float tb = isSel ? 1.00f : 0.48f;
                const char* label = isBack ? "< BACK" : gExtras[i - 1].c_str();
                Text::draw(label, 0.08f, y0 + (itemH - 0.010f - textSize) * 0.5f,
                           textSize, tr, tg, tb);
                if (isSel && !isBack)
                    Text::draw(">", 0.92f, y0 + (itemH - 0.010f - textSize) * 0.5f,
                               textSize * 0.8f, 1.00f, 0.78f, 0.10f);
            }

            if (cnt == 0) {
                float es = fminf(0.028f, 0.70f * asp / 18);
                Text::draw("NO ITEMS FOUND", 0.05f, startY + itemH + 0.02f, es, 0.35f, 0.35f, 0.40f);
            }

            float fs2 = fminf(0.022f, 0.90f * asp / 26);
            Text::draw("VOL navigate   PWR select", 0.05f, 0.945f - fs2,
                       fs2, 0.18f, 0.20f, 0.26f);
            drawRect(0.f, 0.996f, 1.f, 1.f, 0.25f, 0.50f, 1.0f, 0.7f);
            return;
        }

        // ── main menu ─────────────────────────────────────────────────────

        float itemH  = 0.095f;
        float startY = 0.175f;
        int maxLen   = 0;
        for (int i = 0; i < ITEM_COUNT; i++) {
            int l = strlen(ITEMS[i].label);
            if (l > maxLen) maxLen = l;
        }
        float textSize = fminf(0.032f, 0.75f * asp / maxLen);
        float dimA     = (curScreen == Screen::CONFIRM) ? 0.25f : 1.0f;

        for (int i = 0; i < ITEM_COUNT; i++) {
            const BootItem& it = ITEMS[i];
            float y0 = startY + i * itemH;
            float y1 = y0 + itemH - 0.010f;
            bool isSel = (i == curMenuSel);

            if (isSel) {
                drawRoundRect(0.03f, y0, 0.97f, y1,
                    it.ar*0.18f, it.ag*0.18f, it.ab*0.18f, 0.55f*dimA);
                drawRoundRect(0.030f, y0, 0.038f, y1, it.ar, it.ag, it.ab, dimA, 0.004f);
            } else {
                drawRoundRect(0.03f, y0, 0.97f, y1, 0.07f, 0.07f, 0.09f, 0.65f*dimA);
            }

            float iconSize = textSize * 0.95f;
            float iconX = 0.055f, iconY = y0 + (itemH-0.010f-iconSize)*0.5f;
            float ir = isSel ? it.ar : it.ar*0.5f;
            float ig = isSel ? it.ag : it.ag*0.5f;
            float ib = isSel ? it.ab : it.ab*0.5f;
            Text::draw(it.icon, iconX, iconY, iconSize, ir*dimA, ig*dimA, ib*dimA);

            float labelX = iconX + 2.2f*iconSize/asp;
            float labelY = y0 + (itemH-0.010f-textSize)*0.5f;
            float tr = isSel ? 0.92f : 0.40f;
            float tg = isSel ? 0.96f : 0.43f;
            float tb = isSel ? 1.00f : 0.48f;
            Text::draw(it.label, labelX, labelY, textSize, tr*dimA, tg*dimA, tb*dimA);
            if (isSel)
                Text::draw(">", 0.92f, labelY, textSize*0.8f, it.ar*dimA, it.ag*dimA, it.ab*dimA);
        }

        // progress bar
        {
            float barY0  = startY + ITEM_COUNT * itemH + 0.012f;
            float lbSize = fminf(0.022f, 0.85f * asp / 34);
            Text::draw("ANDROID IS BOOTING IN BACKGROUND", 0.05f, barY0, lbSize,
                       0.22f*dimA, 0.50f*dimA, 0.28f*dimA);
            float bgY0 = barY0 + lbSize + 0.006f, bgY1 = bgY0 + 0.018f;
            drawRoundRect(0.05f, bgY0, 0.95f, bgY1, 0.08f, 0.10f, 0.08f, dimA, 0.009f);
            const float trackX0 = 0.05f, trackX1 = 0.95f;
            const float trackW  = trackX1 - trackX0;
            const float bandW   = 0.26f * trackW;
            const float travel  = trackW - bandW;
            float period  = 1.6f;
            float elapsed = now - startTime;
            float cycle   = fmodf(elapsed, period * 2.0f);
            float u       = (cycle < period) ? cycle / period : 1.0f - (cycle - period) / period;
            float u2 = u * u, u3 = u2 * u;
            float pos = u3 * (u * (u * 6.0f - 15.0f) + 10.0f);
            float bx0 = trackX0 + pos * travel;
            float bx1 = bx0 + bandW;
            drawRoundRect(bx0, bgY0, bx1, bgY1, 0.15f, 0.75f*dimA, 0.30f, 0.85f*dimA, 0.009f);
        }

        // footer
        {
            const char* hint = "VOL navigate   TOUCH select   PWR confirm";
            float fs2 = fminf(0.022f, 0.90f * asp / (int)strlen(hint));
            const char* copy = "(C) 2026 BrotherBoard  GPLv3";
            float vs2 = fminf(0.018f, 0.90f * asp / (int)strlen(copy));
            float hintY = 0.945f - fs2;
            Text::draw(hint, 0.05f, hintY, fs2, 0.18f*dimA, 0.20f*dimA, 0.26f*dimA);
            float cpW      = strlen(copy) * vs2 / asp;
            float cpX      = 0.95f - cpW;
            float cpMargin = 0.05f;
            float cpY      = 1.0f - vs2 - cpMargin / 4;
            Text::draw(copy, cpX, cpY, vs2, 0.16f*dimA, 0.22f*dimA, 0.32f*dimA);
        }
        drawRect(0.f, 0.996f, 1.f, 1.f, 0.25f, 0.50f, 1.0f, 0.7f);

        // confirm dialog
        if (curScreen == Screen::CONFIRM && curPending >= 0) {
            const BootItem& pit = ITEMS[curPending];

            drawRect(0.f, 0.f, 1.f, 1.f, 0.0f, 0.0f, 0.0f, 0.55f);

            float dx0=0.08f, dx1=0.92f, dy0=0.36f, dy1=0.70f;
            drawRoundRect(dx0, dy0, dx1, dy1, 0.08f, 0.09f, 0.11f, 0.97f, 0.018f);

            float stripH = 0.032f;
            drawRoundRect(dx0, dy0, dx1, dy0+stripH, pit.ar*0.55f, pit.ag*0.55f, pit.ab*0.55f, 1.0f, 0.018f);
            drawRect(dx0, dy0+stripH*0.5f, dx1, dy0+stripH, pit.ar*0.55f, pit.ag*0.55f, pit.ab*0.55f, 1.0f);

            float qSize  = fminf(0.028f, 0.75f * asp / (int)strlen(pit.confirmMsg));
            float qWidth = strlen(pit.confirmMsg) * qSize / asp;
            float qX     = 0.5f - qWidth * 0.5f;
            Text::draw(pit.confirmMsg, qX, dy0 + stripH + 0.018f, qSize, 0.90f, 0.92f, 1.00f);

            {
                const char* hintStr = "VOL toggle   PWR confirm";
                float hSize  = fminf(0.020f, 0.75f * asp / (int)strlen(hintStr));
                float hWidth = strlen(hintStr) * hSize / asp;
                float hX     = 0.5f - hWidth * 0.5f;
                Text::draw(hintStr, hX, dy0 + stripH + 0.018f + qSize + 0.010f,
                           hSize, 0.28f, 0.30f, 0.40f);
            }

            float btnY0=0.555f, btnY1=0.640f;

            bool yesSel = (curDlgSel == 0);
            drawRoundRect(0.10f, btnY0, 0.44f, btnY1,
                yesSel ? pit.ar*0.35f : 0.10f,
                yesSel ? pit.ag*0.35f : 0.10f,
                yesSel ? pit.ab*0.35f : 0.10f,
                0.95f, 0.014f);
            if (yesSel)
                drawRoundRect(0.100f, btnY0, 0.112f, btnY1, pit.ar, pit.ag, pit.ab, 1.0f, 0.005f);
            {
                float ySize = fminf(0.030f, 0.45f * asp / 3);
                float yW    = 3 * ySize / asp;
                Text::draw("YES", 0.10f + (0.34f - yW)*0.5f, btnY0 + (btnY1-btnY0-ySize)*0.5f,
                           ySize, yesSel?1.f:0.45f, yesSel?1.f:0.45f, yesSel?1.f:0.45f);
            }

            bool noSel = (curDlgSel == 1);
            drawRoundRect(0.56f, btnY0, 0.90f, btnY1,
                noSel ? 0.40f : 0.10f,
                noSel ? 0.10f : 0.10f,
                noSel ? 0.10f : 0.10f,
                0.95f, 0.014f);
            if (noSel)
                drawRoundRect(0.560f, btnY0, 0.572f, btnY1, 0.85f, 0.25f, 0.25f, 1.0f, 0.005f);
            {
                float nSize = fminf(0.030f, 0.45f * asp / 2);
                float nW    = 2 * nSize / asp;
                Text::draw("NO", 0.56f + (0.34f - nW)*0.5f, btnY0 + (btnY1-btnY0-nSize)*0.5f,
                           nSize, noSel?1.f:0.45f, noSel?0.45f:0.45f, noSel?0.45f:0.45f);
            }
        }
    });

    s.run();
    return 0;
}
