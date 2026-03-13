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

float hash(float n) { return fract(sin(n) * 43758.5453); }
float hash2(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

void main() {
    vec2 uv = vUV;
    float row = floor(uv.y * 40.0);
    float glitchTime = floor(time * 8.0);
    float shift = 0.0;
    if (hash(row + glitchTime) > 0.85)
        shift = (hash(row + glitchTime + 1.0) - 0.5) * 0.2;
    uv.x = fract(uv.x + shift);
    vec3 col;
    float block = floor(time * 12.0);
    float r = hash2(vec2(floor(uv.x * 20.0), floor(uv.y * 40.0)) + block);
    float g = hash2(vec2(floor(uv.x * 20.0) + 1.0, floor(uv.y * 40.0)) + block);
    float b = hash2(vec2(floor(uv.x * 20.0), floor(uv.y * 40.0) + 1.0) + block);
    float isGlitch = step(0.92, hash(row + glitchTime));
    col = mix(
        vec3(0.05, 0.05, 0.1),
        vec3(r, g, b),
        isGlitch
    );
    float scan = step(0.98, fract(uv.y * 80.0 - time * 2.0));
    col += vec3(0.0, 1.0, 0.5) * scan * 0.5;
    gl_FragColor = vec4(col, 1.0);
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
