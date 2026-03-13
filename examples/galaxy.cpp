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

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

void main() {
    vec2 uv = vUV - 0.5;
    float angle = atan(uv.y, uv.x);
    float dist = length(uv);
    float spiral = mod(angle / 6.2832 + dist * 3.0 - time * 0.2, 1.0);
    float arm = smoothstep(0.05, 0.0, abs(fract(spiral) - 0.5) - 0.1);
    float star = hash(floor(uv * 40.0));
    star = step(0.97, star) * (0.5 + 0.5 * sin(time * 3.0 + star * 10.0));
    float bright = arm * exp(-dist * 3.0) + star * 0.8;
    vec3 col = mix(vec3(0.0, 0.0, 0.1), vec3(0.6, 0.8, 1.0), bright);
    col += vec3(0.3, 0.1, 0.5) * arm * exp(-dist * 5.0);
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
