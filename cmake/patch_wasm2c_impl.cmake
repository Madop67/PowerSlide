# Relax wasm2c's call_indirect signature check in the generated guest.
#
# Decompiled game code calls some callbacks through function pointer types that
# differ from the callee's definition (e.g. an int-returning LInC registered in a
# void-returning slot). The native i686/x86 builds rely on the C ABI tolerating
# this, but wasm traps on any call_indirect signature mismatch. Keep the table
# bounds and null checks, and drop only the exact-signature comparison so the
# sandbox behaves like the native builds.
#
# Usage: cmake -DIMPL_HEADER=<ctr_guest-impl.h> -P patch_wasm2c_impl.cmake

file(READ "${IMPL_HEADER}" content)

set(original "func_types_eq(ft, table.data[x].func_type)) || \\")
set(replacement "((void)(ft), 1)) || \\")

string(FIND "${content}" "${original}" found)
if(found EQUAL -1)
    message(FATAL_ERROR "patch_wasm2c_impl: CHECK_CALL_INDIRECT pattern not found in ${IMPL_HEADER}; wasm2c output changed")
endif()

string(REPLACE "${original}" "${replacement}" content "${content}")
file(WRITE "${IMPL_HEADER}" "${content}")
