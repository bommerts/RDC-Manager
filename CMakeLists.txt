cmake_minimum_required(VERSION 3.20)
project(RDCManager VERSION 1.1.0 LANGUAGES C RC)

if(NOT MSVC)
    message(FATAL_ERROR "RDC Manager is built with the Microsoft C compiler (MSVC).")
endif()

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Static compiler runtime, so the exe needs nothing installed.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)

# Optimise for size, not speed.
string(REPLACE "/O2" "/O1" CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}")

# Warnings are errors (CMake 3.24+; pass --compile-no-warning-as-error to relax).
if(NOT DEFINED CMAKE_COMPILE_WARNING_AS_ERROR)
    set(CMAKE_COMPILE_WARNING_AS_ERROR ON)
endif()

add_compile_options(/W4 /utf-8 /sdl /guard:cf /Gy /Gw)
add_compile_definitions(UNICODE _UNICODE)

# "Hybrid CRT": the small compiler-specific runtime is linked in, while the big
# Universal C Runtime comes from Windows itself (it ships with Windows 10+).
# This keeps the exe tiny without needing any redistributable.
set(RDCM_LINK_OPTIONS
    /NODEFAULTLIB:libucrt$<$<CONFIG:Debug>:d>.lib
    /DEFAULTLIB:ucrt$<$<CONFIG:Debug>:d>.lib
    /guard:cf
    $<$<NOT:$<CONFIG:Debug>>:/OPT:REF>
    $<$<NOT:$<CONFIG:Debug>>:/OPT:ICF>)
if(CMAKE_C_COMPILER_ARCHITECTURE_ID STREQUAL "x64")
    list(APPEND RDCM_LINK_OPTIONS /CETCOMPAT)       # hardware shadow stacks (x64 only)
endif()

# ---- core: everything that isn't user interface (shared with the tests)
add_library(rdcm_core STATIC
    src/rdcm.h
    src/util.c
    src/model.c
    src/regfile.c
    src/rdpfile.c
    src/cleanup.c)
target_link_libraries(rdcm_core PUBLIC advapi32 shell32 shlwapi ole32 user32)

# ---- the app
add_executable(RDCManager WIN32
    src/main.c
    src/demo.c
    src/ui.h
    src/ui_dark.c
    src/ui_main.c
    res/rdcm.rc
    res/app.manifest)
target_link_libraries(RDCManager PRIVATE rdcm_core comctl32 uxtheme dwmapi gdi32)
# DEPENDENTLOADFLAG 0x800 = load DLLs from System32 only, so a DLL planted
# next to the exe (e.g. in Downloads) can never be loaded.
target_link_options(RDCManager PRIVATE ${RDCM_LINK_OPTIONS} /DEPENDENTLOADFLAG:0x800)

# ---- tests
add_executable(rdcm_tests tests/tests.c)
target_link_libraries(rdcm_tests PRIVATE rdcm_core)
target_link_options(rdcm_tests PRIVATE ${RDCM_LINK_OPTIONS})

enable_testing()
add_test(NAME core COMMAND rdcm_tests)
