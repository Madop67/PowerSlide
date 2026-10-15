// Host-side OpenGL bridge for the wasm sandbox build.
//
// The guest always speaks desktop OpenGL 3.3 core. macOS and Linux pass the
// calls straight through; Android has OpenGL ES 3.0 only, so host_gles.h
// redirects the few calls ES lacks or restricts to shims in host_gles.c.

#ifndef CTR_WASM_HOST_GL_H
#define CTR_WASM_HOST_GL_H

#if defined(__APPLE__)
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#elif defined(__ANDROID__)
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
#else
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glext.h>
#endif

#include "host_mem.h"

#if defined(__ANDROID__)
#include "host_gles.h"
#endif

void HostGL_ShaderSource(struct w2c_env *env, u32 shader, GLsizei count, u32 strings, u32 lengths);

#endif
