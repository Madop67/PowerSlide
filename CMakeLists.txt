cmake_minimum_required(VERSION 3.20)

# NOTE: Must be set before project(). Also the app bundle's LSMinimumSystemVersion.
if(CMAKE_HOST_APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version" FORCE)
endif()

project(CTR-Native C CXX)
include(CheckCCompilerFlag)

set(CTR_NATIVE_VERSION "1.1.1")

# NOTE: macOS cannot run 32-bit executables, so it builds the game as a wasm32
# (ILP32) sandbox translated to native C. See cmake/ctr_wasm_sandbox.cmake.
if(APPLE)
    option(CTR_NATIVE_SANDBOX "Build the game as a wasm32 sandbox (required on macOS)" ON)
else()
    option(CTR_NATIVE_SANDBOX "Build the game as a wasm32 sandbox (required on macOS)" OFF)
endif()

# First-run disc setup and a per-user data folder for packaged builds
# (platform/setup/). The macOS app bundle has its own AppKit setup window.
if(WIN32 OR (CTR_NATIVE_SANDBOX AND NOT APPLE))
    option(CTR_NATIVE_SETUP "Build first-run disc setup into the executable" ON)
else()
    option(CTR_NATIVE_SETUP "Build first-run disc setup into the executable" OFF)
endif()

if(NOT CTR_NATIVE_SANDBOX AND NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR
        "CTR Native currently requires a 32-bit target. "
        "Select Win32 for MSVC, an i686 MinGW compiler on Windows, or pass -m32 "
        "in the initial compiler and linker flags on Linux."
    )
endif()

set(CTR_NATIVE_MSVC_FRONTEND OFF)
if(CMAKE_C_COMPILER_ID STREQUAL "MSVC" OR CMAKE_C_SIMULATE_ID STREQUAL "MSVC")
    set(CTR_NATIVE_MSVC_FRONTEND ON)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# NOTE: Android keeps the NDK toolchain's search modes, so the cross-build
# never picks up the build machine's libraries.
if(NOT ANDROID)
    set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE NEVER)
    set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY NEVER)
    set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE NEVER)
endif()

execute_process(
    COMMAND git rev-parse --short=12 HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE CTR_NATIVE_GIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT CTR_NATIVE_GIT_HASH)
    set(CTR_NATIVE_GIT_HASH "unknown")
endif()

execute_process(
    COMMAND git diff --quiet --exit-code
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    RESULT_VARIABLE CTR_NATIVE_GIT_DIRTY
    ERROR_QUIET
)
if(CTR_NATIVE_GIT_DIRTY EQUAL 0)
    set(CTR_NATIVE_BUILD_ID "${CTR_NATIVE_GIT_HASH}")
else()
    set(CTR_NATIVE_BUILD_ID "${CTR_NATIVE_GIT_HASH}-dirty")
endif()

function(ctr_native_add_label_warning_if_supported target warning flag_check)
    set(saved_required_flags "${CMAKE_REQUIRED_FLAGS}")
    if(CMAKE_C_COMPILER_ID MATCHES "Clang")
        set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} -Werror=unknown-warning-option")
    endif()

    check_c_compiler_flag("${warning}" "${flag_check}")
    set(CMAKE_REQUIRED_FLAGS "${saved_required_flags}")

    if(${flag_check})
        target_compile_options(${target} PRIVATE "${warning}")
    endif()
endfunction()

# SDL3
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL_GPU OFF CACHE BOOL "" FORCE)
set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
set(SDL_SENSOR OFF CACHE BOOL "" FORCE)
set(SDL_TRAY OFF CACHE BOOL "" FORCE)
set(SDL_POWER OFF CACHE BOOL "" FORCE)
set(SDL_VULKAN OFF CACHE BOOL "" FORCE)
# NOTE: Disc setup uses SDL's file dialog and draws its progress window with
# SDL_Renderer (software or OpenGL only).
set(SDL_DIALOG ${CTR_NATIVE_SETUP} CACHE BOOL "" FORCE)
set(SDL_RENDER ${CTR_NATIVE_SETUP} CACHE BOOL "" FORCE)
set(SDL_RENDER_D3D OFF CACHE BOOL "" FORCE)
set(SDL_RENDER_D3D11 OFF CACHE BOOL "" FORCE)
set(SDL_RENDER_D3D12 OFF CACHE BOOL "" FORCE)
set(SDL_RENDER_METAL OFF CACHE BOOL "" FORCE)
set(SDL_RENDER_VULKAN OFF CACHE BOOL "" FORCE)
# Android has OpenGL ES only; the wasm host maps the game's GL 3.3 onto it.
if(ANDROID)
    set(SDL_OPENGLES ON CACHE BOOL "" FORCE)
    # SDL's Java side reaches USB controllers; no libusb in the APK.
    set(SDL_HIDAPI_LIBUSB OFF CACHE BOOL "" FORCE)
else()
    set(SDL_OPENGLES OFF CACHE BOOL "" FORCE)
endif()
set(SDL_OFFSCREEN OFF CACHE BOOL "" FORCE)
set(SDL_SNDIO OFF CACHE BOOL "" FORCE)
set(SDL_SNDIO_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_X11_XSCRNSAVER OFF CACHE BOOL "" FORCE)
set(SDL_X11_XTEST OFF CACHE BOOL "" FORCE)
add_subdirectory(externals/SDL)

# Embed the UI font as a byte array so the executable stays self-contained.
# NOTE: Shared with the wasm sandbox build, which also compiles the settings UI.
set(CTR_NATIVE_UI_FONT ${CMAKE_SOURCE_DIR}/externals/fonts/fredoka/Fredoka-SemiBold.ttf)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CTR_NATIVE_UI_FONT})
file(READ ${CTR_NATIVE_UI_FONT} ctr_ui_font_hex HEX)
string(LENGTH "${ctr_ui_font_hex}" ctr_ui_font_hex_length)
set(ctr_ui_font_source "// Generated by CMakeLists.txt from externals/fonts/fredoka/Fredoka-SemiBold.ttf.\n")
string(APPEND ctr_ui_font_source "static const unsigned char kNativeUiFontData[] = {\n")
set(ctr_ui_font_offset 0)
while(ctr_ui_font_offset LESS ctr_ui_font_hex_length)
    string(SUBSTRING "${ctr_ui_font_hex}" ${ctr_ui_font_offset} 64 ctr_ui_font_line)
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," ctr_ui_font_line "${ctr_ui_font_line}")
    string(APPEND ctr_ui_font_source "${ctr_ui_font_line}\n")
    math(EXPR ctr_ui_font_offset "${ctr_ui_font_offset} + 64")
endwhile()
string(APPEND ctr_ui_font_source "};\n")
file(WRITE ${CMAKE_BINARY_DIR}/generated/native_ui_font.h.tmp "${ctr_ui_font_source}")
configure_file(${CMAKE_BINARY_DIR}/generated/native_ui_font.h.tmp ${CMAKE_BINARY_DIR}/generated/native_ui_font.h COPYONLY)

# libchdr decodes .chd disc images during first-run setup (never in the game).
if(CTR_NATIVE_SETUP OR CTR_NATIVE_SANDBOX)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(INSTALL_STATIC_LIBS OFF CACHE BOOL "" FORCE)
    set(WITH_SYSTEM_ZLIB OFF CACHE BOOL "" FORCE)
    set(WITH_SYSTEM_ZSTD OFF CACHE BOOL "" FORCE)
    set(CHDR_WANT_TESTS OFF CACHE BOOL "" FORCE)
    add_subdirectory(externals/libchdr EXCLUDE_FROM_ALL)
endif()

set(CTR_SETUP_SOURCES
    platform/setup/disc_import.c
    platform/setup/native_setup.c
)

if(CTR_NATIVE_SANDBOX)
    include(cmake/ctr_wasm_sandbox.cmake)
    return()
endif()

# In-game settings UI: Dear ImGui is C++, so it builds as its own library and the
# game code stays C17. Only platform/native_ui.cpp and native_second_screen_ui.cpp talk to ImGui.
add_library(ctr_native_ui STATIC
    platform/native_ui.cpp
    platform/native_second_screen_ui.cpp
    externals/imgui/imgui.cpp
    externals/imgui/imgui_draw.cpp
    externals/imgui/imgui_tables.cpp
    externals/imgui/imgui_widgets.cpp
    externals/imgui/backends/imgui_impl_sdl3.cpp
    externals/imgui/backends/imgui_impl_opengl3.cpp
)
set_target_properties(ctr_native_ui PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)
target_include_directories(ctr_native_ui PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/externals/imgui
    ${CMAKE_SOURCE_DIR}/externals/imgui/backends
    ${CMAKE_BINARY_DIR}/generated
)

target_compile_definitions(ctr_native_ui PRIVATE
    IMGUI_DISABLE_OBSOLETE_FUNCTIONS
    CTR_NATIVE_VERSION=\"${CTR_NATIVE_VERSION}\"
    CTR_NATIVE_BUILD_ID=\"${CTR_NATIVE_BUILD_ID}\"
)
target_link_libraries(ctr_native_ui PUBLIC SDL3::SDL3 ${CMAKE_DL_LIBS})

# CTR Native
add_executable(ctr_native main.c)
if(WIN32)
    # Executable icon for Explorer and shortcuts, and the manifest (UTF-8 code page).
    target_sources(ctr_native PRIVATE platform/windows/ctr_native.rc)
    set_property(SOURCE platform/windows/ctr_native.rc APPEND PROPERTY OBJECT_DEPENDS
        ${CMAKE_SOURCE_DIR}/platform/windows/ctr_native.manifest
        ${CMAKE_SOURCE_DIR}/platform/windows/powerslide.ico
    )

    # A GUI program, so double-clicking it opens no console window. main.c
    # attaches to the parent console when started from a terminal.
    set_target_properties(ctr_native PROPERTIES WIN32_EXECUTABLE ON)
    if(CTR_NATIVE_MSVC_FRONTEND)
        target_link_options(ctr_native PRIVATE /ENTRY:mainCRTStartup /MANIFEST:NO)
    endif()
endif()
if(CTR_NATIVE_SETUP)
    target_sources(ctr_native PRIVATE ${CTR_SETUP_SOURCES})
    target_compile_definitions(ctr_native PRIVATE CTR_NATIVE_SETUP)
    target_include_directories(ctr_native PRIVATE ${CMAKE_SOURCE_DIR}/platform)
    target_link_libraries(ctr_native chdr-static)
endif()
set_target_properties(ctr_native PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
target_include_directories(ctr_native PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_compile_definitions(ctr_native PRIVATE
    CTR_NATIVE
    CTR_INTERNAL
    CTR_NATIVE_VERSION=\"${CTR_NATIVE_VERSION}\"
    CTR_NATIVE_BUILD_ID=\"${CTR_NATIVE_BUILD_ID}\"
)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # 32-bit stat() fails with EOVERFLOW on filesystems with 64-bit inode numbers
    # (XFS, btrfs, WSL's /mnt/c), so replay files and folders look missing there.
    target_compile_definitions(ctr_native PRIVATE _FILE_OFFSET_BITS=64)
endif()
if(CTR_NATIVE_MSVC_FRONTEND)
    target_compile_options(ctr_native PRIVATE
        /W4 #TODO(aalhendi): fix warnings
        /bigobj
        /arch:SSE2
        /wd4152
        /wd4244
        /wd4245
        /wd4310
        /wd5287
    )
else()
    target_compile_options(ctr_native PRIVATE
        -msse
        -Wenum-conversion
        -Wenum-compare
        -Wswitch-enum
        -Wformat
        -Wparentheses
        $<$<CONFIG:Release>:-O2>
        $<$<CONFIG:Release>:-g>
    )

    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        target_compile_options(ctr_native PRIVATE -Wall -Wextra -Wstrict-aliasing=2)
        ctr_native_add_label_warning_if_supported(ctr_native -Wfree-labels CTR_HAS_WFREE_LABELS)
    elseif(CMAKE_C_COMPILER_ID MATCHES "Clang")
        ctr_native_add_label_warning_if_supported(ctr_native -Wc23-extensions CTR_HAS_WC23_EXTENSIONS)
    endif()
endif()
target_link_libraries(ctr_native ctr_native_ui SDL3::SDL3)
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_link_options(ctr_native PRIVATE -static-libgcc -static-libstdc++)
endif()
if(MINGW)
    target_link_options(ctr_native PRIVATE -static)
endif()

include(CTest)
if(BUILD_TESTING)
    add_test(NAME ctr_native_version COMMAND $<TARGET_FILE:ctr_native> --version)
    set_tests_properties(ctr_native_version PROPERTIES PASS_REGULAR_EXPRESSION "^PowerSlide ")

    find_package(Python3 REQUIRED COMPONENTS Interpreter)

    # Release executables must not need any library a player would have to
    # install (CRT, SDL, libchdr, ...).
    if(WIN32)
        add_test(
            NAME ctr_native_standalone
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/release/check_standalone.py $<TARGET_FILE:ctr_native>
        )
    endif()
    add_test(
        NAME ctr_match_unit
        COMMAND ${Python3_EXECUTABLE} -m unittest discover
            -s ${CMAKE_SOURCE_DIR}/tools/matching
            -p test_*.py
    )

    # Plays every power-on replay in debug/replay-suite with default settings and fails
    # on any divergence. Skipped when there is no suite (it needs your own disc).
    if(NOT CTR_NATIVE_SANDBOX)
        add_test(
            NAME ctr_replay_suite
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/replays/replay_suite.py
                run --exe $<TARGET_FILE:ctr_native> --skip-code 77
        )
        set_tests_properties(ctr_replay_suite PROPERTIES SKIP_RETURN_CODE 77)
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        add_executable(ctr_particle_renderer_smoke
            tests/particle_renderer_smoke.c
            platform/native_gpu_links.c
        )
        set_target_properties(ctr_particle_renderer_smoke PROPERTIES
            C_STANDARD 17
            C_STANDARD_REQUIRED ON
            C_EXTENSIONS OFF
        )
        target_include_directories(ctr_particle_renderer_smoke PRIVATE ${CMAKE_SOURCE_DIR}/include)
        target_compile_definitions(ctr_particle_renderer_smoke PRIVATE CTR_NATIVE)
        target_compile_options(ctr_particle_renderer_smoke PRIVATE
            -O2 -Wall -Wextra -ffunction-sections -fdata-sections
        )
        target_link_options(ctr_particle_renderer_smoke PRIVATE -Wl,--gc-sections)
        add_test(NAME ctr_particle_renderer_smoke COMMAND $<TARGET_FILE:ctr_particle_renderer_smoke>)

        add_executable(ctr_gte_gpl_shift_regression
            tests/gte_gpl_shift_regression.c
            platform/native_gte_core.c
        )
        set_target_properties(ctr_gte_gpl_shift_regression PROPERTIES
            C_STANDARD 17
            C_STANDARD_REQUIRED ON
            C_EXTENSIONS OFF
        )
        target_include_directories(ctr_gte_gpl_shift_regression PRIVATE ${CMAKE_SOURCE_DIR}/include)
        target_compile_definitions(ctr_gte_gpl_shift_regression PRIVATE CTR_NATIVE)
        target_compile_options(ctr_gte_gpl_shift_regression PRIVATE
            -O2 -Wall -Wextra -fsanitize=undefined -fno-sanitize-recover=all
        )
        target_link_options(ctr_gte_gpl_shift_regression PRIVATE -fsanitize=undefined -fno-sanitize-recover=all)
        add_test(NAME ctr_gte_gpl_shift_regression COMMAND $<TARGET_FILE:ctr_gte_gpl_shift_regression>)
    endif()
endif()
