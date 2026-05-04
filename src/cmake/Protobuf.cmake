# Fetches protobuf v3.21.12, builds both libprotobuf (full, Debug) and
# libprotobuf-lite (Release), plus libprotoc + protoc for code generation.
#
# Debug  → links libprotobuf      (heavier, DebugString works)
# Release → links libprotobuf-lite (lightweight)

if(TARGET libprotobuf AND TARGET libprotobuf-lite)
    return()
endif()

include(FetchCache)
include(FetchContent)

set(protobuf_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(protobuf_BUILD_CONFORMANCE OFF CACHE BOOL "" FORCE)
set(protobuf_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(protobuf_WITH_ZLIB OFF CACHE BOOL "" FORCE)
set(protobuf_MSVC_STATIC_RUNTIME ON CACHE BOOL "" FORCE)
set(protobuf_BUILD_LIBPROTOBUF ON CACHE BOOL "" FORCE)
set(protobuf_BUILD_PROTOC_BINARIES ON CACHE BOOL "" FORCE)

FetchContent_Declare(
    protobuf
    GIT_REPOSITORY https://github.com/protocolbuffers/protobuf.git
    GIT_TAG        v3.21.12
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)

set(_saved_CMAKE_WARN_DEPRECATED ${CMAKE_WARN_DEPRECATED})
set(CMAKE_WARN_DEPRECATED OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(protobuf)
set(CMAKE_WARN_DEPRECATED ${_saved_CMAKE_WARN_DEPRECATED} CACHE BOOL "" FORCE)

# Silence warnings in protobuf source.
foreach(_t IN ITEMS libprotobuf libprotobuf-lite libprotoc)
    if(TARGET ${_t})
        if(MSVC)
            target_compile_options(${_t} PRIVATE /w)
        else()
            target_compile_options(${_t} PRIVATE -w)
        endif()
        target_compile_definitions(${_t} PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
endforeach()
