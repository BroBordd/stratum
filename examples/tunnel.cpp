#include "Stratum.h"
#include <GLES2/gl2.h>

static const char* VSH = R"(
attribute vec2 pos;
attribute vec2 uv;
varying vec2 vUV;
void main() {
    vUV = uv;
    gl_Position = vec4(pos, 0.0, 1.0);
}
)";

static const char* FSH = R"(
precision mediump float;
varying vec2 vUV;
uniform float time;

vec3 hsv2rgb(vec3 c) {
    vec4 K = vec4(1.0, 2.0/3.0, 1.0/3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

void main() {
    vec2 uv = vUV - 0.5;
    float angle = atan(uv.y, uv.x) / 6.2832;
    float dist = 0.3 / length(uv);
    float h = mod(angle + dist - time * 0.2, 1.0);
    float stripe = mod(dist - time * 0.5, 1.0);
    float bright = step(0.5, stripe) * 0.7 + 0.3;
    gl_FragColor = vec4(hsv2rgb(vec3(h, 0.9, bright)), 1.0);
}
)";

GLuint compileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    return s;
}

int main() {
    Stratum s;
    if (!s.init()) return 1;

    GLuint vs = compileShader(GL_VERTEX_SHADER, VSH);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, FSH);
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glUseProgram(prog);

    GLint posLoc  = glGetAttribLocation(prog, "pos");
    GLint uvLoc   = glGetAttribLocation(prog, "uv");
    GLint timeLoc = glGetUniformLocation(prog, "time");

    static const float verts[] = {
        -1, -1,  0, 1,
         1, -1,  1, 1,
        -1,  1,  0, 0,
         1,  1,  1, 0,
    };

    glVertexAttribPointer(posLoc, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), verts);
    glVertexAttribPointer(uvLoc,  2, GL_FLOAT, GL_FALSE, 4*sizeof(float), verts+2);
    glEnableVertexAttribArray(posLoc);
    glEnableVertexAttribArray(uvLoc);

    s.onFrame([&](float t) {
        if (t > 5.0f) { s.stop(); return; }
        glUniform1f(timeLoc, t);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    });

    s.run();
    return 0;
}
