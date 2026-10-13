// Native entrypoint for the macOS wasm sandbox build.
//
// Instantiates the wasm2c-translated guest module (game + platform code built
// for wasm32-wasip1), wires up the WASI/SDL/GL imports, and runs `_start`.

#include <stdarg.h>
#include <string.h>
#include <unistd.h>

#include "host_mem.h"
#include "host_wasi.h"

#include "wasm-rt-impl.h"

w2c_ctr g_hostGuest;

static struct w2c_env s_hostEnv;
static struct w2c_wasi__snapshot__preview1 s_hostWasi;

void HostMem_Fatal(const char *fmt, ...)
{
	va_list args;

	fflush(stdout);
	fprintf(stderr, "[CTR Native] fatal (wasm host): ");
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
	fprintf(stderr, "\n");
	abort();
}

uint32_t HostMem_Alloc(uint32_t size)
{
	uint32_t offset = w2c_ctr_ctr_guest_alloc(&g_hostGuest, size);

	if (offset == 0)
	{
		HostMem_Fatal("guest allocation of %u bytes failed", size);
	}

	return offset;
}

void HostMem_Free(uint32_t offset)
{
	if (offset != 0)
	{
		w2c_ctr_ctr_guest_free(&g_hostGuest, offset);
	}
}

struct HostStringCacheEntry
{
	struct HostStringCacheEntry *next;
	uint32_t guestOffset;
	char *text;
};

static struct HostStringCacheEntry *s_hostStringCache;

uint32_t HostMem_StringToGuest(struct w2c_env *env, const char *str)
{
	(void)env;

	if (str == NULL)
	{
		return 0;
	}

	for (struct HostStringCacheEntry *entry = s_hostStringCache; entry != NULL; entry = entry->next)
	{
		if (strcmp(entry->text, str) == 0)
		{
			return entry->guestOffset;
		}
	}

	size_t len = strlen(str) + 1;
	struct HostStringCacheEntry *entry = malloc(sizeof(*entry));
	if (entry == NULL)
	{
		HostMem_Fatal("out of host memory");
	}

	entry->text = strdup(str);
	entry->guestOffset = HostMem_Alloc((uint32_t)len);
	memcpy(HostMem_G2H(entry->guestOffset, len), str, len);
	entry->next = s_hostStringCache;
	s_hostStringCache = entry;

	return entry->guestOffset;
}

#define HOST_HANDLE_MAX 256

static void *s_hostHandles[HOST_HANDLE_MAX];

uint32_t HostHandle_FromPtr(void *ptr)
{
	if (ptr == NULL)
	{
		return 0;
	}

	uint32_t freeSlot = 0;
	for (uint32_t i = 1; i < HOST_HANDLE_MAX; i++)
	{
		if (s_hostHandles[i] == ptr)
		{
			return i;
		}

		if ((freeSlot == 0) && (s_hostHandles[i] == NULL))
		{
			freeSlot = i;
		}
	}

	if (freeSlot == 0)
	{
		HostMem_Fatal("out of host handles");
	}

	s_hostHandles[freeSlot] = ptr;
	return freeSlot;
}

void *HostHandle_ToPtr(uint32_t handle)
{
	if (handle == 0)
	{
		return NULL;
	}

	if ((handle >= HOST_HANDLE_MAX) || (s_hostHandles[handle] == NULL))
	{
		HostMem_Fatal("invalid host handle %u", handle);
	}

	return s_hostHandles[handle];
}

void HostHandle_Release(uint32_t handle)
{
	if ((handle != 0) && (handle < HOST_HANDLE_MAX))
	{
		s_hostHandles[handle] = NULL;
	}
}

int main(int argc, char **argv)
{
	char cwd[4096];

	// NOTE: The guest's constructor chdir()s here so relative asset paths match
	// the native builds; WASI itself starts in "/".
	if (getcwd(cwd, sizeof(cwd)) != NULL)
	{
		setenv("CTR_HOST_CWD", cwd, 1);
	}

	HostWasi_Init(argc, argv);

	wasm_rt_init();

	s_hostEnv.instance = &g_hostGuest;
	s_hostWasi.instance = &g_hostGuest;
	wasm2c_ctr_instantiate(&g_hostGuest, &s_hostEnv, &s_hostWasi);

	int exitCode = 0;
	wasm_rt_trap_t trap = wasm_rt_impl_try();
	if (trap == WASM_RT_TRAP_NONE)
	{
		w2c_ctr_0x5Fstart(&g_hostGuest);
	}
	else
	{
		// NOTE: Best effort; the guest may be in a bad state after a trap.
		if (wasm_rt_impl_try() == WASM_RT_TRAP_NONE)
		{
			w2c_ctr_ctr_guest_flush(&g_hostGuest);
		}

		fflush(stdout);
		fprintf(stderr, "[CTR Native] guest trapped: %s\n", wasm_rt_strerror(trap));
		exitCode = 1;
	}

	wasm2c_ctr_free(&g_hostGuest);
	wasm_rt_free();

	return exitCode;
}
