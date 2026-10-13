// Shared helpers for the native host of the macOS wasm sandbox build.
//
// The game and platform code run as a wasm32 module translated to C by wasm2c
// ("the guest"). Guest pointers are 32-bit offsets into the module's linear
// memory; these helpers convert them to host pointers, map opaque SDL objects
// to 32-bit handles, and copy host strings into guest memory.

#ifndef CTR_WASM_HOST_MEM_H
#define CTR_WASM_HOST_MEM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "ctr_guest.h"

// NOTE: wasm2c leaves the import module structs for the embedder to define.
struct w2c_env
{
	w2c_ctr *instance;
};

struct w2c_wasi__snapshot__preview1
{
	w2c_ctr *instance;
};

extern w2c_ctr g_hostGuest;

static inline uint8_t *HostMem_Base(void)
{
	return w2c_ctr_memory(&g_hostGuest)->data;
}

static inline uint64_t HostMem_Size(void)
{
	return w2c_ctr_memory(&g_hostGuest)->size;
}

_Noreturn void HostMem_Fatal(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

// Guest offset -> host pointer, checking that [offset, offset + size) is in bounds.
// A zero offset is the guest NULL pointer and maps to NULL.
static inline void *HostMem_G2H(uint32_t offset, uint64_t size)
{
	if (offset == 0)
	{
		return NULL;
	}

	if (((uint64_t)offset + size) > HostMem_Size())
	{
		HostMem_Fatal("guest pointer out of bounds: 0x%08x + %llu", offset, (unsigned long long)size);
	}

	return HostMem_Base() + offset;
}

#define HOST_G2H(env, offset)         ((void)(env), HostMem_G2H((offset), 1))
#define HOST_G2H_SIZED(offset, size)  HostMem_G2H((offset), (size))

uint32_t HostMem_Alloc(uint32_t size);
void HostMem_Free(uint32_t offset);

// Copies a NUL-terminated host string into guest memory. Results are cached per
// distinct string contents, so repeated calls (SDL_GetError, glGetString) do
// not leak guest memory. Returns 0 for NULL.
uint32_t HostMem_StringToGuest(struct w2c_env *env, const char *str);

// Opaque host object <-> 32-bit guest handle. Handle 0 is NULL.
uint32_t HostHandle_FromPtr(void *ptr);
void *HostHandle_ToPtr(uint32_t handle);
void HostHandle_Release(uint32_t handle);

#endif
