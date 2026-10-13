// Guest-side support for the macOS wasm sandbox build (CTR_NATIVE_WASM).
// Included from main.c. See platform/wasm_host/ for the native host.

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// NOTE: Exported so the host can place returned strings and arrays (SDL_GetError,
// glGetString, SDL_GetGamepads, ...) in guest linear memory.
__attribute__((export_name("ctr_guest_alloc"))) void *CtrGuest_Alloc(size_t size)
{
	return malloc(size);
}

__attribute__((export_name("ctr_guest_free"))) void CtrGuest_Free(void *ptr)
{
	free(ptr);
}

// NOTE: The guest's cwd lives inside wasi-libc. The host resolves relative
// paths it receives (SDL_SaveBMP, SDL_AddGamepadMappingsFromFile) against it.
__attribute__((export_name("ctr_guest_getcwd"))) const char *CtrGuest_GetCwd(void)
{
	static char cwd[1024];

	return (getcwd(cwd, sizeof(cwd)) != NULL) ? cwd : NULL;
}

// NOTE: Called by the host after a trap so buffered guest logs are not lost.
__attribute__((export_name("ctr_guest_flush"))) void CtrGuest_Flush(void)
{
	fflush(NULL);
}

// NOTE: WASI starts with cwd "/". The host passes its real cwd so relative
// paths like assets/ctr-u.bin resolve the same way as the native builds.
__attribute__((constructor)) static void CtrGuest_RestoreHostCwd(void)
{
	const char *cwd = getenv("CTR_HOST_CWD");

	if ((cwd != NULL) && (cwd[0] != '\0'))
	{
		(void)chdir(cwd);
	}
}

// NOTE: SDL's stdinc helpers live in libSDL3, which the guest does not link.
// The platform layer only needs these few; wasi-libc provides the real work.
int SDL_snprintf(char *text, size_t maxlen, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	const int result = vsnprintf(text, maxlen, fmt, args);
	va_end(args);
	return result;
}

float SDL_floorf(float x)
{
	return floorf(x);
}

float SDL_sqrtf(float x)
{
	return sqrtf(x);
}
