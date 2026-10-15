// OpenGL ES 3.0 shims for the Android wasm host. See host_gl.h.
//
// The game and ImGui are built against desktop GL 3.3 core. On ES these calls
// are missing, take fewer formats, or need an extension; each shim keeps the
// desktop behaviour where ES can provide it and is a quiet no-op otherwise.

#ifndef CTR_WASM_HOST_GLES_H
#define CTR_WASM_HOST_GLES_H

// Desktop-only enums the guest passes through.
#define HOST_GL_POLYGON_MODE      0x0B40
#define HOST_GL_FILL              0x1B02
#define HOST_GL_TIME_ELAPSED      0x88BF
#define HOST_GL_BGRA              0x80E1
#define HOST_GL_PRIMITIVE_RESTART 0x8F9D
#define HOST_GL_CONTEXT_PROFILE_MASK 0x9126

// Called once a context is current: looks up the extension entry points.
void HostGLES_InitContext(void);

void HostGLES_PolygonMode(GLenum face, GLenum mode);
void HostGLES_GetIntegerv(GLenum pname, GLint *data);
void HostGLES_Enable(GLenum cap);
void HostGLES_Disable(GLenum cap);
GLboolean HostGLES_IsEnabled(GLenum cap);
void HostGLES_BeginQuery(GLenum target, GLuint id);
void HostGLES_EndQuery(GLenum target);
void HostGLES_GetQueryObjectiv(GLuint id, GLenum pname, GLint *params);
void HostGLES_GetQueryObjectuiv(GLuint id, GLenum pname, GLuint *params);
void HostGLES_ReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, void *pixels);

// NOTE: host_gles.c defines HOST_GLES_IMPL to call the real functions.
#if !defined(HOST_GLES_IMPL)
#define glPolygonMode       HostGLES_PolygonMode
#define glGetIntegerv       HostGLES_GetIntegerv
#define glEnable            HostGLES_Enable
#define glDisable           HostGLES_Disable
#define glIsEnabled         HostGLES_IsEnabled
#define glBeginQuery        HostGLES_BeginQuery
#define glEndQuery          HostGLES_EndQuery
#define glGetQueryObjectiv  HostGLES_GetQueryObjectiv
#define glGetQueryObjectuiv HostGLES_GetQueryObjectuiv
#define glReadPixels        HostGLES_ReadPixels
#endif

#endif
