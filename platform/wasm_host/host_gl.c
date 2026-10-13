// Hand-written OpenGL imports for the macOS wasm sandbox build. The rest are
// generated into host_gl.gen.c by tools/macos/gen_wasm_gl_bridge.py.

#include "host_gl.h"

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

	glShaderSource((GLuint)shader, count, hostStrings, (const GLint *)HOST_G2H_SIZED(lengths, (u64)count * 4));
}
