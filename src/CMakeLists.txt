cmake_minimum_required(VERSION 3.20)
project(OpenSteamTool VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

# Allow CMAKE_MSVC_RUNTIME_LIBRARY to control runtime selection for all targets,
# including dependencies pulled in via FetchContent.
if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()

# Static MSVC runtime everywhere, so the resulting DLL has no extra runtime
# dependencies. Must be set BEFORE FetchContent_MakeAvailable so the fetched
# deps (Lua, Detours, spdlog) inherit it.
set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "" FORCE)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# ---------------------------------------------------------------------------
# Dependency recipes (FetchContent-backed, cached at <repo>/.deps).
# ---------------------------------------------------------------------------
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Lua)
include(Detours)
include(Spdlog)
include(Protobuf)
include(Tomlplusplus)
include(LogMacros)

# ---------------------------------------------------------------------------
# Protobuf code generation — two variants from the same .proto:
#
#   Debug  → full Message  (protoc --cpp_out)         → links libprotobuf
#   Release → lite MessageLite (protoc --cpp_out=lite) → links libprotobuf-lite
#
# Both land in separate subdirectories of the build tree so the source
# directory stays clean and the right set is picked per configuration.
# ---------------------------------------------------------------------------
set(PROTO_SRC "${CMAKE_CURRENT_SOURCE_DIR}/proto/steam_messages.proto")
set(PROTO_GEN_DIR       "${CMAKE_CURRENT_BINARY_DIR}/proto")
set(PROTO_GEN_LITE_DIR  "${CMAKE_CURRENT_BINARY_DIR}/proto_lite")

# Full Message (Debug)
add_custom_command(
    OUTPUT "${PROTO_GEN_DIR}/steam_messages.pb.cc"
           "${PROTO_GEN_DIR}/steam_messages.pb.h"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PROTO_GEN_DIR}"
    COMMAND $<TARGET_FILE:protoc>
        "--cpp_out=${PROTO_GEN_DIR}"
        "-I${CMAKE_CURRENT_SOURCE_DIR}/proto"
        "${PROTO_SRC}"
    DEPENDS "${PROTO_SRC}" protoc
    COMMENT "Generating protobuf full-Message sources (Debug)"
)

# Lite MessageLite (Release)
add_custom_command(
    OUTPUT "${PROTO_GEN_LITE_DIR}/steam_messages.pb.cc"
           "${PROTO_GEN_LITE_DIR}/steam_messages.pb.h"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PROTO_GEN_LITE_DIR}"
    COMMAND $<TARGET_FILE:protoc>
        "--cpp_out=lite:${PROTO_GEN_LITE_DIR}"
        "-I${CMAKE_CURRENT_SOURCE_DIR}/proto"
        "${PROTO_SRC}"
    DEPENDS "${PROTO_SRC}" protoc
    COMMENT "Generating protobuf lite-MessageLite sources (Release)"
)

# ---------------------------------------------------------------------------
# OpenSteamTool — the hook DLL injected into Steam (always 64-bit).
# ---------------------------------------------------------------------------
add_library(OpenSteamTool SHARED
    dllmain.cpp

    # Shared utilities
    Utils/AppTicket.cpp
    Utils/ByteSearch.cpp
    Utils/Log.cpp
    Utils/Config.cpp
    Utils/LuaConfig.cpp
    Utils/VehCommon.cpp
    Utils/WinHttp.cpp
    Utils/FileWatcher.cpp

    # Per-category hook modules
    Hook/HookManager.cpp
    Hook/Hooks_AccessToken.cpp
    Hook/Hooks_AppState.cpp
    Hook/Hooks_AppTicket.cpp
    Hook/Hooks_Decryption.cpp
    Hook/Hooks_IPC.cpp
    Hook/Hooks_IPC_ISteamUser.cpp
    Hook/Hooks_IPC_ISteamUtils.cpp
    Hook/Hooks_KeyValues.cpp
    Hook/Hooks_Manifest.cpp
    Hook/Hooks_Misc.cpp
    Hook/Hooks_NetPacket.cpp
    Hook/Hooks_SteamUI.cpp
    Hook/Hooks_Package.cpp

    # protobuf generated sources — per-config variant
    $<$<CONFIG:Debug>:${PROTO_GEN_DIR}/steam_messages.pb.cc>
    $<$<CONFIG:Release>:${PROTO_GEN_LITE_DIR}/steam_messages.pb.cc>
)

# Header search path — per-config include directory
target_include_directories(OpenSteamTool PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/generated
    $<$<CONFIG:Debug>:${PROTO_GEN_DIR}>
    $<$<CONFIG:Release>:${PROTO_GEN_LITE_DIR}>
)

target_link_libraries(OpenSteamTool PRIVATE
    lua_static
    detours
    winhttp
    $<$<CONFIG:Debug>:libprotobuf>
    $<$<CONFIG:Release>:libprotobuf-lite>
    tomlplusplus::tomlplusplus
    $<$<CONFIG:Debug>:spdlog::spdlog>
)

# Logging is compiled in only for Debug; Release reduces LOG_* to no-ops.
target_compile_definitions(OpenSteamTool PRIVATE
    $<$<CONFIG:Debug>:OPENSTEAMTOOL_LOGGING_ENABLED>
)

# ---------------------------------------------------------------------------
# dwmapi.dll hijack — small loader DLL placed alongside Steam.
# ---------------------------------------------------------------------------
add_library(dwmapi SHARED
    dwmapi/dwmapi.cpp
)

# ---------------------------------------------------------------------------
# xinput1_4.dll hijack — secondary loader DLL placed alongside Steam.
# ---------------------------------------------------------------------------
add_library(xinput1_4 SHARED
    xinput4_1/xinput1_4.cpp
    xinput4_1/xinput1_4.def
)
target_link_libraries(xinput1_4 PRIVATE detours)
