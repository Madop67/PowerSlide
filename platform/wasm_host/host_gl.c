// Hand-written OpenGL imports for the wasm sandbox build. The rest are
// generated into host_gl.gen.c by tools/macos/gen_wasm_gl_bridge.py.

#include "host_gl.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#if defined(__ANDROID__)
// Joins the guest's shader strings and swaps its desktop "#version 1x0/330"
// line for GLSL ES 3.00 with default precisions (ES fragment shaders have no
// default float precision). The game and ImGui shaders are otherwise written
// to compile as either. Returns a malloc'd string.
static char *HostGL_ToGlslEs(GLsizei count, const GLchar *const *strings, const GLint *lengths)
{
	static const char kEsHeader[] = "#version 300 es\nprecision highp float;\nprecision highp int;\nprecision highp sampler2D;\n";
	size_t total = sizeof(kEsHeader);

	for (GLsizei i = 0; i < count; i++)
	{
		total += ((lengths != NULL) && (lengths[i] >= 0)) ? (size_t)lengths[i] : strlen(strings[i]);
	}

	char *joined = malloc(total);
	char *source = malloc(total);
	if ((joined == NULL) || (source == NULL))
	{
		HostMem_Fatal("glShaderSource: out of host memory");
	}

	size_t used = 0;
	for (GLsizei i = 0; i < count; i++)
	{
		size_t len = ((lengths != NULL) && (lengths[i] >= 0)) ? (size_t)lengths[i] : strlen(strings[i]);
		memcpy(&joined[used], strings[i], len);
		used += len;
	}
	joined[used] = '\0';

	// Drop the first #version line, which only whitespace may precede.
	const char *body = joined;
	while (isspace((unsigned char)*body))
	{
		body++;
	}

	if (strncmp(body, "#version", 8) == 0)
	{
		body = strchr(body, '\n');
		body = (body != NULL) ? body + 1 : "";
	}
	else
	{
		body = joined;
	}

	snprintf(source, total, "%s%s", kEsHeader, body);
	free(joined);
	return source;
}
#endif

void HostGL_ShaderSource(struct w2c_env *env, u32 shader, GLsizei count, u32 strings, u32 lengths)
{
	(void)env;
	const GLchar *hostStrings[16];

	if ((count < 0) || (count > 16))
	{
		HostMem_Fatal("glShaderSource: unsupported string count %d", (int)count);
	}

	// NOTE: The guest passes an array of 32-bit string offsets.
	const uint32_t *guestStrings = HOST_G2H_SIZED(strings, (u64)count * 4);
	for (GLsizei i = 0; i < count; i++)
	{
		hostStrings[i] = (const GLchar *)HOST_G2H_SIZED(guestStrings[i], 1);
	}

	const GLint *hostLengths = (lengths != 0) ? (const GLint *)HOST_G2H_SIZED(lengths, (u64)count * 4) : NULL;

#if defined(__ANDROID__)
	char *source = HostGL_ToGlslEs(count, hostStrings, hostLengths);
	glShaderSource((GLuint)shader, 1, (const GLchar *const *)&source, NULL);
	free(source);
#else
	glShaderSource((GLuint)shader, count, hostStrings, hostLengths);
#endif
}
