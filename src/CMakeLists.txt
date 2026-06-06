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
set(OPENSTEAMTOOL_TOOLS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../tools")
include(Lua)
include(Detours)
include(Spdlog)
include(Protobuf)
include(Tomlplusplus)
include(LogMacros)

# Build the host code generators (ipc_codegen) in their own binary subtree so
# their executables never land in the shipped output directory. EXCLUDE_FROM_ALL
# means only the tools actually depended on (ipc_codegen) get built here.
add_subdirectory("${OPENSTEAMTOOL_TOOLS_DIR}" "${CMAKE_BINARY_DIR}/tools" EXCLUDE_FROM_ALL)
include("${OPENSTEAMTOOL_TOOLS_DIR}/cmake/IPCCodegen.cmake")

set(IPC_IDL "${CMAKE_CURRENT_SOURCE_DIR}/Steam/IPCMessages.steamd")
# Per-config output dir, mirroring the protobuf recipe below: a Multi-Config
# build invokes the generator once per config, and a single shared output file
# would ping-pong between the Release/Debug graphs and force needless rebuilds.
opensteamtool_add_ipc_codegen(IPC_GEN
    IDL "${IPC_IDL}"
    CPP_OUT "${CMAKE_CURRENT_BINARY_DIR}/generated/$<CONFIG>"
)

# ---------------------------------------------------------------------------
# Protobuf code generation — one variant per config, from the same .proto:
#
#   Debug   → full Message     (protoc --cpp_out)       → links libprotobuf
#   Release → lite MessageLite (protoc --cpp_out=lite:) → links libprotobuf-lite
#
# Output lands in the per-config generated/ dir (same root as the IPC header),
# so each config owns its own files. A shared output path would ping-pong
# between the Release/Debug graphs (the command embeds the config-specific
# protoc path) and force a regen + recompile on every config switch.
# ---------------------------------------------------------------------------
set(PROTO_SRC "${CMAKE_CURRENT_SOURCE_DIR}/proto/steam_messages.proto")
set(PROTO_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/$<CONFIG>")

add_custom_command(
    OUTPUT "${PROTO_GEN_DIR}/steam_messages.pb.cc"
           "${PROTO_GEN_DIR}/steam_messages.pb.h"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PROTO_GEN_DIR}"
    COMMAND $<TARGET_FILE:protoc>
        "--cpp_out=$<$<CONFIG:Release>:lite:>${PROTO_GEN_DIR}"
        "-I${CMAKE_CURRENT_SOURCE_DIR}/proto"
        "${PROTO_SRC}"
    DEPENDS "${PROTO_SRC}" protoc
    COMMENT "Generating protobuf C++ sources"
    VERBATIM
)

# ---------------------------------------------------------------------------
# OpenSteamTool — the hook DLL injected into Steam (always 64-bit).
# ---------------------------------------------------------------------------
add_library(OpenSteamTool SHARED
    dllmain.cpp

    # Shared utilities
    Utils/AppTicket.cpp
    Utils/ByteSearch.cpp
    Utils/PatternLoader.cpp
    Utils/Log.cpp
    Utils/Config.cpp
    Utils/LuaConfig.cpp
    Utils/VehCommon.cpp
    Utils/WinHttp.cpp
    Utils/FileWatcher.cpp
    Utils/ManifestClient.cpp
    Utils/RemoteToml.cpp
    Utils/IPCLoader.cpp
    Utils/SteamDiagnostics.cpp

    # Generated IPC message structs (depends on IPCMessages.steamd via add_custom_command)
    "${IPC_GEN}"

    # Per-category hook modules
    Hook/HookManager.cpp
    Hook/Hooks_CallBack.cpp
    Hook/Hooks_Decryption.cpp
    Hook/Hooks_Inject.cpp
    Hook/Hooks_IPC.cpp
    Hook/Hooks_IPC_ISteamUser.cpp
    Hook/Hooks_IPC_ISteamUtils.cpp
    Hook/Hooks_KeyValues.cpp
    Hook/Hooks_Manifest.cpp
    Hook/Hooks_Misc.cpp
    Hook/Hooks_NetPacket.cpp
    Hook/PendingAPICalls.cpp
    Hook/Hooks_SteamUI.cpp
    Hook/Hooks_Package.cpp

    # protobuf generated sources (full in Debug, lite in Release)
    "${PROTO_GEN_DIR}/steam_messages.pb.cc"
)

# Header search path — per-config include directory
target_include_directories(OpenSteamTool PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/generated
    ${CMAKE_CURRENT_BINARY_DIR}/generated/$<CONFIG>
)

target_link_libraries(OpenSteamTool PRIVATE
    lua_static
    detours
    winhttp
    Bcrypt
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
    xinput1_4/xinput1_4.cpp
    xinput1_4/xinput1_4.def
)

# ---------------------------------------------------------------------------
# OnlineFix — DLL injected into a game launched with -onlinefix
# ---------------------------------------------------------------------------
add_library(OnlineFix SHARED
    Payload/payload.cpp
    Payload/PayloadLog.cpp
    Payload/EosBridge.cpp
    Payload/SelfPropagate.cpp
)
set_target_properties(OnlineFix PROPERTIES PREFIX "" OUTPUT_NAME "OnlineFix")
target_include_directories(OnlineFix PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(OnlineFix PRIVATE detours psapi)

# Logging is compiled in only for Debug, same as the host DLL.
target_compile_definitions(OnlineFix PRIVATE
    $<$<CONFIG:Debug>:OPENSTEAMTOOL_LOGGING_ENABLED>
)
