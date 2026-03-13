#include "Stratum.h"
#include <GLES2/gl2.h>
#include <math.h>

static const char* VSH = R"(
attribute vec2 pos;
void main() {
    gl_Position = vec4(pos, 0.0, 1.0);
}
)";

static const char* FSH = R"(
precision mediump float;
uniform float time;
uniform vec3 color;

void main() {
    float brightness = 0.5 + 0.5 * sin(time * 2.0);
    gl_FragColor = vec4(color * brightness, 1.0);
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

    GLint posLoc   = glGetAttribLocation(prog, "pos");
    GLint timeLoc  = glGetUniformLocation(prog, "time");
    GLint colorLoc = glGetUniformLocation(prog, "color");

    static const float verts[] = {
        -1, -1,
         1, -1,
        -1,  1,
         1,  1,
    };

    glVertexAttribPointer(posLoc, 2, GL_FLOAT, GL_FALSE, 0, verts);
    glEnableVertexAttribArray(posLoc);

    s.onFrame([&](float t) {
        if (t > 5.0f) { s.stop(); return; }
        float r = 0.5f + 0.5f * sinf(t * 0.3f);
        float g = 0.5f + 0.5f * sinf(t * 0.3f + 2.094f);
        float b = 0.5f + 0.5f * sinf(t * 0.3f + 4.188f);
        glUniform1f(timeLoc, t);
        glUniform3f(colorLoc, r, g, b);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    });

    s.run();
    return 0;
}
