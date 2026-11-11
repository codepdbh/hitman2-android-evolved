cmake_minimum_required(VERSION 3.22)
project(h2ae C)

# Repository root: android/app/src/main/cpp -> five levels up.
get_filename_component(H2_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)
set(H2_GEN_INC "${H2_ROOT}/build/generated/include")

# The portable cpu.h is derived from PCRecomp's at build time (never committed).
find_program(H2_PYTHON NAMES python python3 py)
if(H2_PYTHON AND EXISTS "${H2_ROOT}/third_party/pcrecomp/runtime/recomp32_cpu/cpu.h")
    execute_process(COMMAND "${H2_PYTHON}" "${H2_ROOT}/tools/gen_portable_cpu.py"
                    RESULT_VARIABLE H2_GEN_RC OUTPUT_QUIET)
    if(NOT H2_GEN_RC EQUAL 0)
        message(FATAL_ERROR "tools/gen_portable_cpu.py failed (${H2_GEN_RC})")
    endif()
endif()
if(NOT EXISTS "${H2_GEN_INC}/cpu.h")
    message(FATAL_ERROR "build/generated/include/cpu.h is missing: clone PCRecomp into third_party/pcrecomp "
                        "and run `python tools/gen_portable_cpu.py` (see README).")
endif()

add_library(h2ae SHARED
    h2ae_jni.c
    "${H2_ROOT}/runtime/guest_mem.c"
    "${H2_ROOT}/runtime/h2_runtime.c"
    "${H2_ROOT}/runtime/h2_selftest.c")

target_include_directories(h2ae PRIVATE "${H2_ROOT}/runtime" "${H2_GEN_INC}")
target_compile_definitions(h2ae PRIVATE H2AE_ABI="${ANDROID_ABI}")
target_compile_options(h2ae PRIVATE -std=c11 -fno-strict-aliasing -fwrapv -Wall -Wno-unused-function)
target_link_libraries(h2ae PRIVATE log m)
# Devices with 16 KB pages (Android 15+).
target_link_options(h2ae PRIVATE "-Wl,-z,max-page-size=16384")

# ---------------------------------------------------------------------------
# libh2game.so: the recompiled game (lifted C generated locally from the user's own binaries; never committed
# and never distributed) + runtime + platform + renderer + input + Android glue. Built only if the lifted code
# exists (python scripts/lift_image.py lift).
# ---------------------------------------------------------------------------
file(GLOB H2_LIFTED CONFIGURE_DEPENDS "${H2_ROOT}/recomp/generated/hitman2/chunk_*.c" "${H2_ROOT}/recomp/generated/renderopengl/chunk_*.c")
if(H2_LIFTED AND EXISTS "${H2_ROOT}/recomp/generated/funcs_all.h")
    # The lifted code is ~1.1 M lines: keep the build polite (at most 4 compile jobs).
    set_property(GLOBAL PROPERTY JOB_POOLS h2_pool=4)
    set(CMAKE_JOB_POOL_COMPILE h2_pool)
    add_library(h2game SHARED
        h2ae_game.c
        "${H2_ROOT}/runtime/guest_mem.c" "${H2_ROOT}/runtime/h2_runtime.c" "${H2_ROOT}/runtime/h2_dispatch.c"
        "${H2_ROOT}/platform/imports.c" "${H2_ROOT}/platform/pe_loader.c" "${H2_ROOT}/platform/guest_alloc.c"
        "${H2_ROOT}/platform/process.c" "${H2_ROOT}/platform/win32_kernel32.c" "${H2_ROOT}/platform/win32_objects.c"
        "${H2_ROOT}/platform/win32_threads.c" "${H2_ROOT}/platform/win32_files.c" "${H2_ROOT}/platform/win32_user32.c"
        "${H2_ROOT}/platform/game_dlls.c" "${H2_ROOT}/platform/dinput8.c" "${H2_ROOT}/platform/win32_advapi32.c"
        "${H2_ROOT}/platform/dsound.c" "${H2_ROOT}/input/input_manager.c"
        "${H2_ROOT}/renderer/h2_gl.c" "${H2_ROOT}/renderer/gl_null_backend.c" "${H2_ROOT}/renderer/gl_gles3_backend.c"
        ${H2_LIFTED})
    target_include_directories(h2game PRIVATE "${H2_ROOT}/runtime" "${H2_ROOT}/platform" "${H2_ROOT}/input"
                               "${H2_ROOT}/renderer" "${H2_ROOT}/recomp/generated" "${H2_GEN_INC}")
    target_compile_options(h2game PRIVATE -std=c11 -O1 -fno-strict-aliasing -fwrapv -w)
    target_link_libraries(h2game PRIVATE log m android EGL GLESv3)
    target_link_options(h2game PRIVATE "-Wl,-z,max-page-size=16384")
else()
    message(STATUS "h2game: no lifted code found, libh2game.so is not built (run scripts/lift_image.py lift)")
endif()
