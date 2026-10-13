# macOS (arm64) build: run the 32-bit game as a wasm32 sandbox translated to C.
#
# 1. main.c (the unity build of game + platform code) and the C++ settings UI
#    (platform/native_ui.cpp + Dear ImGui) are compiled for wasm32-wasip1 with
#    wasi-sdk. wasm32 is ILP32, so every retail struct layout assert and the
#    in-place 32-bit asset pointer patching keep working.
# 2. wasm2c turns the module back into C.
# 3. That C, the wasm2c runtime and the host layer in platform/wasm_host/
#    (WASI, SDL3 and OpenGL imports) build one native executable.
#
# Toolchain: tools/macos/fetch-toolchain.sh installs the pinned wasi-sdk and
# WABT into externals/.toolchains/.

set(CTR_WASI_SDK_DIR "${CMAKE_SOURCE_DIR}/externals/.toolchains/wasi-sdk" CACHE PATH "wasi-sdk install directory")
set(CTR_WABT_DIR "${CMAKE_SOURCE_DIR}/externals/.toolchains/wabt" CACHE PATH "WABT install directory")
set(CTR_WASM2C_OUTPUTS 8 CACHE STRING "Number of C files wasm2c splits the guest into")

set(CTR_WASI_CLANG "${CTR_WASI_SDK_DIR}/bin/clang")
set(CTR_WASI_CLANGXX "${CTR_WASI_SDK_DIR}/bin/clang++")
set(CTR_WASM2C "${CTR_WABT_DIR}/bin/wasm2c")
set(CTR_WASM2C_RT_DIR "${CMAKE_SOURCE_DIR}/externals/wasm2c-rt")

foreach(tool IN ITEMS "${CTR_WASI_CLANG}" "${CTR_WASI_CLANGXX}" "${CTR_WASM2C}")
    if(NOT EXISTS "${tool}")
        message(FATAL_ERROR
            "Missing ${tool}.\n"
            "Run tools/macos/fetch-toolchain.sh (or ./build-macos.sh) to install the pinned wasm toolchain."
        )
    endif()
endforeach()

# ---- Guest: main.c + settings UI -> ctr_guest.wasm --------------------------

set(CTR_GUEST_DIR "${CMAKE_BINARY_DIR}/guest")
set(CTR_GUEST_WASM "${CTR_GUEST_DIR}/ctr_guest.wasm")
file(MAKE_DIRECTORY "${CTR_GUEST_DIR}")

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CTR_GUEST_OPT -O1 -g)
else()
    set(CTR_GUEST_OPT -O2 -g)
endif()

set(CTR_GUEST_COMMON_FLAGS
    --target=wasm32-wasip1
    --sysroot=${CTR_WASI_SDK_DIR}/share/wasi-sysroot
    ${CTR_GUEST_OPT}
    -DCTR_NATIVE_WASM
    "-DCTR_NATIVE_VERSION=\"${CTR_NATIVE_VERSION}\""
    "-DCTR_NATIVE_BUILD_ID=\"${CTR_NATIVE_BUILD_ID}\""
    -I${CMAKE_SOURCE_DIR}/include
    -I${CMAKE_SOURCE_DIR}/externals/SDL/include
    # NOTE: setjmp/longjmp (ThTick in game/PROC.c) lowers to wasm exception handling.
    -mexception-handling
    -mllvm -wasm-enable-sjlj
)

set(CTR_GUEST_C_FLAGS
    ${CTR_GUEST_COMMON_FLAGS}
    -std=c17
    -DCTR_NATIVE
    -DCTR_INTERNAL
    -I${CMAKE_SOURCE_DIR}
    -Wenum-conversion
    -Wenum-compare
    -Wswitch-enum
    -Wformat
    -Wparentheses
)

# NOTE: The ImGui OpenGL3 backend uses the guest's GL imports (native_glad.h ->
# guest_gl.h) instead of its own GL3W loader, which needs dlopen.
set(CTR_GUEST_CXX_FLAGS
    ${CTR_GUEST_COMMON_FLAGS}
    -std=c++17
    -fno-exceptions
    -fno-rtti
    -DIMGUI_DISABLE_OBSOLETE_FUNCTIONS
    # NOTE: WASI has no fork/exec; the SDL3 backend opens URLs via SDL_OpenURL.
    -DIMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS
    -DIMGUI_IMPL_OPENGL_LOADER_CUSTOM
    -include platform/native_glad.h
    -I${CMAKE_SOURCE_DIR}/externals/imgui
    -I${CMAKE_SOURCE_DIR}/externals/imgui/backends
    -I${CMAKE_BINARY_DIR}/generated
)

set(CTR_GUEST_CXX_SOURCES
    platform/native_ui.cpp
    externals/imgui/imgui.cpp
    externals/imgui/imgui_draw.cpp
    externals/imgui/imgui_tables.cpp
    externals/imgui/imgui_widgets.cpp
    externals/imgui/backends/imgui_impl_sdl3.cpp
    externals/imgui/backends/imgui_impl_opengl3.cpp
)

set(CTR_GUEST_OBJECTS)

function(ctr_guest_compile source compiler)
    get_filename_component(name "${source}" NAME_WE)
    set(object "${CTR_GUEST_DIR}/${name}.o")
    add_custom_command(
        OUTPUT "${object}"
        COMMAND "${compiler}" ${ARGN}
                -MD -MF "${object}.d" -MT "${object}"
                -c "${CMAKE_SOURCE_DIR}/${source}"
                -o "${object}"
        DEPENDS "${CMAKE_SOURCE_DIR}/${source}"
        DEPFILE "${object}.d"
        COMMENT "Compiling ${source} to wasm32 (wasi-sdk)"
        VERBATIM
    )
    set(CTR_GUEST_OBJECTS ${CTR_GUEST_OBJECTS} "${object}" PARENT_SCOPE)
endfunction()

ctr_guest_compile(main.c "${CTR_WASI_CLANG}" ${CTR_GUEST_C_FLAGS})
foreach(source IN LISTS CTR_GUEST_CXX_SOURCES)
    ctr_guest_compile("${source}" "${CTR_WASI_CLANGXX}" ${CTR_GUEST_CXX_FLAGS})
endforeach()

add_custom_command(
    OUTPUT "${CTR_GUEST_WASM}"
    COMMAND "${CTR_WASI_CLANGXX}"
            --target=wasm32-wasip1
            --sysroot=${CTR_WASI_SDK_DIR}/share/wasi-sysroot
            -fno-exceptions
            ${CTR_GUEST_OBJECTS}
            -o "${CTR_GUEST_WASM}"
            -lsetjmp
            -Wl,--allow-undefined-file=${CMAKE_SOURCE_DIR}/platform/wasm/imports.syms
            -Wl,-z,stack-size=8388608
    DEPENDS ${CTR_GUEST_OBJECTS} "${CMAKE_SOURCE_DIR}/platform/wasm/imports.syms"
    COMMENT "Linking ctr_guest.wasm"
    VERBATIM
)

# ---- wasm2c: ctr_guest.wasm -> ctr_guest_*.c --------------------------------

set(CTR_GUEST_C_FILES)
math(EXPR CTR_WASM2C_LAST "${CTR_WASM2C_OUTPUTS} - 1")
foreach(i RANGE ${CTR_WASM2C_LAST})
    list(APPEND CTR_GUEST_C_FILES "${CTR_GUEST_DIR}/ctr_guest_${i}.c")
endforeach()

add_custom_command(
    OUTPUT ${CTR_GUEST_C_FILES} "${CTR_GUEST_DIR}/ctr_guest.h" "${CTR_GUEST_DIR}/ctr_guest-impl.h"
    COMMAND "${CTR_WASM2C}" --enable-exceptions -n ctr --num-outputs ${CTR_WASM2C_OUTPUTS}
            "${CTR_GUEST_WASM}" -o "${CTR_GUEST_DIR}/ctr_guest.c"
    COMMAND "${CMAKE_COMMAND}" -DIMPL_HEADER=${CTR_GUEST_DIR}/ctr_guest-impl.h
            -P "${CMAKE_SOURCE_DIR}/cmake/patch_wasm2c_impl.cmake"
    DEPENDS "${CTR_GUEST_WASM}" "${CMAKE_SOURCE_DIR}/cmake/patch_wasm2c_impl.cmake"
    COMMENT "Translating wasm to C (wasm2c)"
    VERBATIM
)

# ---- Host executable --------------------------------------------------------

add_executable(ctr_native
    ${CTR_GUEST_C_FILES}
    ${CTR_WASM2C_RT_DIR}/wasm-rt-impl.c
    ${CTR_WASM2C_RT_DIR}/wasm-rt-mem-impl.c
    ${CTR_WASM2C_RT_DIR}/wasm-rt-exceptions-impl.c
    platform/wasm_host/host_main.c
    platform/wasm_host/host_wasi.c
    platform/wasm_host/host_sdl.c
    platform/wasm_host/host_gl.c
    platform/wasm_host/host_gl.gen.c
)
set_target_properties(ctr_native PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
target_include_directories(ctr_native PRIVATE
    "${CTR_GUEST_DIR}"
    "${CTR_WASM2C_RT_DIR}"
    "${CMAKE_SOURCE_DIR}/platform/wasm_host"
)
target_compile_options(ctr_native PRIVATE $<$<CONFIG:Release>:-O2>)
# NOTE: wasm2c output trips many harmless warnings (unused labels/params etc).
set_source_files_properties(${CTR_GUEST_C_FILES} PROPERTIES COMPILE_OPTIONS "-w")
target_link_libraries(ctr_native PRIVATE SDL3::SDL3 "-framework OpenGL")

include(CTest)
if(BUILD_TESTING)
    add_test(NAME ctr_native_version COMMAND $<TARGET_FILE:ctr_native> --version)
    set_tests_properties(ctr_native_version PROPERTIES PASS_REGULAR_EXPRESSION "^PowerSlide ")

    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(
        NAME ctr_match_unit
        COMMAND ${Python3_EXECUTABLE} -m unittest discover
            -s ${CMAKE_SOURCE_DIR}/tools/matching
            -p test_*.py
    )
endif()
