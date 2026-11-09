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
