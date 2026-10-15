// OpenGL ES 3.0 shims for the Android wasm host. See host_gles.h.

#if defined(__ANDROID__)

#define HOST_GLES_IMPL 1
#include "host_gl.h"

#include <EGL/egl.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

// EXT_disjoint_timer_query: GPU timings for the perf overlay, when present.
static PFNGLGETQUERYOBJECTIVEXTPROC s_hostGlesGetQueryObjectiv;
static PFNGLGETQUERYOBJECTUI64VEXTPROC s_hostGlesGetQueryObjectui64v;
static int s_hostGlesHasTimerQuery;
static int s_hostGlesTimerActive;

void HostGLES_InitContext(void)
{
	s_hostGlesHasTimerQuery = SDL_GL_ExtensionSupported("GL_EXT_disjoint_timer_query");
	if (s_hostGlesHasTimerQuery)
	{
		s_hostGlesGetQueryObjectiv = (PFNGLGETQUERYOBJECTIVEXTPROC)eglGetProcAddress("glGetQueryObjectivEXT");
		s_hostGlesGetQueryObjectui64v = (PFNGLGETQUERYOBJECTUI64VEXTPROC)eglGetProcAddress("glGetQueryObjectui64vEXT");
		s_hostGlesHasTimerQuery = (s_hostGlesGetQueryObjectiv != NULL) && (s_hostGlesGetQueryObjectui64v != NULL);
	}

	SDL_Log("[CTR Native] GLES: %s, %s; timer queries %s", (const char *)glGetString(GL_VERSION), (const char *)glGetString(GL_RENDERER),
	        s_hostGlesHasTimerQuery ? "on" : "off");
}

// ---- Fixed-function state ES does not have ----------------------------------

// NOTE: Only the debug wireframe (Shift+F1) and ImGui's state backup use it.
void HostGLES_PolygonMode(GLenum face, GLenum mode)
{
	(void)face;
	(void)mode;
}

void HostGLES_GetIntegerv(GLenum pname, GLint *data)
{
	switch (pname)
	{
	case HOST_GL_POLYGON_MODE:
		data[0] = HOST_GL_FILL;
		data[1] = HOST_GL_FILL;
		return;

	// NOTE: ImGui asks for the desktop profile; ES has none.
	case HOST_GL_CONTEXT_PROFILE_MASK:
		data[0] = 0;
		return;

	default:
		glGetIntegerv(pname, data);
		return;
	}
}

// ES 3.0 always restarts primitives at the maximum index; nothing to switch.
void HostGLES_Enable(GLenum cap)
{
	if (cap != HOST_GL_PRIMITIVE_RESTART)
	{
		glEnable(cap);
	}
}

void HostGLES_Disable(GLenum cap)
{
	if (cap != HOST_GL_PRIMITIVE_RESTART)
	{
		glDisable(cap);
	}
}

GLboolean HostGLES_IsEnabled(GLenum cap)
{
	return (cap == HOST_GL_PRIMITIVE_RESTART) ? GL_FALSE : glIsEnabled(cap);
}

// ---- Timer queries ----------------------------------------------------------

void HostGLES_BeginQuery(GLenum target, GLuint id)
{
	if (target == HOST_GL_TIME_ELAPSED)
	{
		s_hostGlesTimerActive = s_hostGlesHasTimerQuery;
		if (!s_hostGlesTimerActive)
		{
			return;
		}
	}

	glBeginQuery(target, id);
}

void HostGLES_EndQuery(GLenum target)
{
	if ((target == HOST_GL_TIME_ELAPSED) && !s_hostGlesTimerActive)
	{
		return;
	}

	glEndQuery(target);
}

// Without the extension, a timer query is always ready and measured nothing.
void HostGLES_GetQueryObjectiv(GLuint id, GLenum pname, GLint *params)
{
	if (s_hostGlesHasTimerQuery)
	{
		s_hostGlesGetQueryObjectiv(id, pname, params);
		return;
	}

	*params = (pname == GL_QUERY_RESULT_AVAILABLE) ? 1 : 0;
}

void HostGLES_GetQueryObjectuiv(GLuint id, GLenum pname, GLuint *params)
{
	if (!s_hostGlesHasTimerQuery)
	{
		*params = (pname == GL_QUERY_RESULT_AVAILABLE) ? 1u : 0u;
		return;
	}

	if (pname == GL_QUERY_RESULT)
	{
		GLuint64 elapsed = 0;
		s_hostGlesGetQueryObjectui64v(id, pname, &elapsed);
		*params = (elapsed > 0xffffffffu) ? 0xffffffffu : (GLuint)elapsed;
		return;
	}

	glGetQueryObjectuiv(id, pname, params);
}

// ---- Read-back --------------------------------------------------------------

// ES only guarantees RGBA/UNSIGNED_BYTE (plus one implementation format) for
// glReadPixels. The game reads VRAM back as GL_RG and screenshots as GL_BGRA,
// so read RGBA and repack into the caller's layout (GL_PACK_ROW_LENGTH and
// GL_PACK_ALIGNMENT included).
void HostGLES_ReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, void *pixels)
{
	GLint readFormat = 0;
	GLint readType = 0;
	GLint packBuffer = 0;

	glGetIntegerv(GL_IMPLEMENTATION_COLOR_READ_FORMAT, &readFormat);
	glGetIntegerv(GL_IMPLEMENTATION_COLOR_READ_TYPE, &readType);
	glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &packBuffer);

	int direct = (format == GL_RGBA) || ((GLint)format == readFormat && (GLint)type == readType);
	if (direct || (type != GL_UNSIGNED_BYTE) || (packBuffer != 0) || ((format != GL_RG) && (format != HOST_GL_BGRA)) || (width <= 0) || (height <= 0))
	{
		glReadPixels(x, y, width, height, format, type, pixels);
		return;
	}

	GLint rowLength = 0;
	GLint alignment = 4;
	GLint skipRows = 0;
	GLint skipPixels = 0;
	glGetIntegerv(GL_PACK_ROW_LENGTH, &rowLength);
	glGetIntegerv(GL_PACK_ALIGNMENT, &alignment);
	glGetIntegerv(GL_PACK_SKIP_ROWS, &skipRows);
	glGetIntegerv(GL_PACK_SKIP_PIXELS, &skipPixels);

	uint8_t *rgba = malloc((size_t)width * (size_t)height * 4u);
	if (rgba == NULL)
	{
		return;
	}

	glPixelStorei(GL_PACK_ROW_LENGTH, 0);
	glPixelStorei(GL_PACK_ALIGNMENT, 4);
	glPixelStorei(GL_PACK_SKIP_ROWS, 0);
	glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
	glReadPixels(x, y, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
	glPixelStorei(GL_PACK_ROW_LENGTH, rowLength);
	glPixelStorei(GL_PACK_ALIGNMENT, alignment);
	glPixelStorei(GL_PACK_SKIP_ROWS, skipRows);
	glPixelStorei(GL_PACK_SKIP_PIXELS, skipPixels);

	size_t components = (format == GL_RG) ? 2u : 4u;
	size_t rowPixels = (rowLength > 0) ? (size_t)rowLength : (size_t)width;
	size_t stride = rowPixels * components;
	if ((alignment > 1) && (stride % (size_t)alignment) != 0)
	{
		stride += (size_t)alignment - (stride % (size_t)alignment);
	}

	for (GLsizei row = 0; row < height; row++)
	{
		const uint8_t *src = &rgba[(size_t)row * (size_t)width * 4u];
		uint8_t *dst = (uint8_t *)pixels + ((size_t)skipRows + (size_t)row) * stride + (size_t)skipPixels * components;

		for (GLsizei col = 0; col < width; col++, src += 4, dst += components)
		{
			if (format == GL_RG)
			{
				dst[0] = src[0];
				dst[1] = src[1];
			}
			else
			{
				dst[0] = src[2];
				dst[1] = src[1];
				dst[2] = src[0];
				dst[3] = src[3];
			}
		}
	}

	free(rgba);
}

#endif
