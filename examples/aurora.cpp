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

void main() {
    vec2 uv = vUV;
    float band = 0.0;
    band += sin(uv.x * 3.0 + time * 0.5) * 0.15;
    band += sin(uv.x * 5.0 - time * 0.3) * 0.1;
    band += sin(uv.x * 7.0 + time * 0.7) * 0.07;
    float y = uv.y - 0.5 + band;
    float curtain = exp(-y * y * 20.0);
    float shimmer = 0.5 + 0.5 * sin(uv.x * 10.0 + time * 2.0);
    vec3 green  = vec3(0.0, 1.0, 0.4) * curtain;
    vec3 blue   = vec3(0.0, 0.4, 1.0) * curtain * shimmer;
    vec3 purple = vec3(0.6, 0.0, 1.0) * curtain * (1.0 - shimmer);
    vec3 col = green + blue + purple;
    col += vec3(0.0, 0.0, 0.05);
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
