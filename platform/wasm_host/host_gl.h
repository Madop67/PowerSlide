// Host-side OpenGL bridge for the macOS wasm sandbox build.

#ifndef CTR_WASM_HOST_GL_H
#define CTR_WASM_HOST_GL_H

#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>

#include "host_mem.h"

void HostGL_ShaderSource(struct w2c_env *env, u32 shader, GLsizei count, u32 strings, u32 lengths);

#endif
