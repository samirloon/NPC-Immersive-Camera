cmake_minimum_required(VERSION 3.21)

project(
    NPCIC
    VERSION 1.0.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- CommonLibSSE-NG-ng build options (must be set before add_subdirectory) ---
# REX::INI is required for the settings loader; enable all three runtimes so the
# single DLL works on Skyrim SE, AE, and VR.
set(REX_OPTION_INI  ON  CACHE BOOL "Enable REX INI support"   FORCE)
set(ENABLE_SKYRIM_SE ON  CACHE BOOL "Support Skyrim SE"      FORCE)
set(ENABLE_SKYRIM_AE ON  CACHE BOOL "Support Skyrim AE"      FORCE)
set(ENABLE_SKYRIM_VR ON  CACHE BOOL "Support Skyrim VR"       FORCE)
# Tests require Catch2 (not in our vcpkg manifest); disable to keep the build self-contained.
set(BUILD_TESTS    OFF CACHE BOOL "Build CommonLibSSE tests" FORCE)

set(COMMONLIBSSE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../CommonLibSSE-NG-ng
    CACHE PATH "Path to CommonLibSSE-NG-ng sources")

# Add the library as a subdirectory. A separate binary dir avoids the
# library's in-source-build guard. First build compiles CommonLibSSE from
# source (~minutes); subsequent builds reuse it.
add_subdirectory(${COMMONLIBSSE_DIR} ${CMAKE_BINARY_DIR}/_CommonLibSSE)

# NOTE: we do NOT use the add_commonlibsse_plugin() helper, because that
# function lives in the subdirectory's scope and is not reliably visible in
# this parent scope. The target is built manually below and linked against the
# CommonLibSSE::CommonLibSSE alias exported by the subdirectory.

add_library(${PROJECT_NAME} SHARED
    src/PluginInfo.cpp
    src/main.cpp
    src/Settings.cpp
    src/NPCCamera.cpp
    src/InputHandler.cpp
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        CommonLibSSE::CommonLibSSE
        user32  # MapVirtualKeyW for the configurable hotkey
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    OUTPUT_NAME "NPCIC"
    CXX_STANDARD 23
)

# Reuse CommonLibSSE's precompiled header. The RE/REL/SKSE headers depend on
# standard-library includes (cassert, span, bit, array, string_view, ...),
# `using namespace std::literals` (for the `sv` suffix), the `stl` namespace
# alias under RE/SKSE/REL, and the RELOCATION_ID macro -- all defined in
# CommonLibSSE's PCH (include/SKSE/Impl/PCH.h). Declared PRIVATE on the
# CommonLibSSE target, so it does not propagate; reuse it explicitly here.
target_precompile_headers(${PROJECT_NAME} REUSE_FROM CommonLibSSE)

# Copy the INI next to the built DLL for convenience when deploying.
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/NPCIC.ini
    ${CMAKE_BINARY_DIR}/NPCIC.ini
    COPYONLY
)
