cmake_minimum_required(VERSION 3.23)

project(rpcpp
    VERSION 0.1.0
    DESCRIPTION "JSON-RPC 2.0 server library for C++20"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(RPCPP_BUILD_EXAMPLES "Build rpcpp examples" ${PROJECT_IS_TOP_LEVEL})
option(RPCPP_BUILD_TESTS    "Build rpcpp tests"    ${PROJECT_IS_TOP_LEVEL})
option(RPCPP_BUILD_MSGPACK  "Enable the msgpack codec (requires msgpack-c)" OFF)
option(RPCPP_BUILD_QUICKJS  "Enable the QuickJS native codec (fetches quickjs-ng for tests/examples)" OFF)

# ── Dependencies (managed with CPM) ───────────────────────────────────────────
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/get_cpm.cmake)

CPMAddPackage(
    NAME               reflectcpp
    GITHUB_REPOSITORY  getml/reflect-cpp
    VERSION            0.24.0
    OPTIONS
        "REFLECTCPP_MSGPACK ${RPCPP_BUILD_MSGPACK}"
)

# moodycamel's concurrentqueue: header-only, consumed as <concurrentqueue.h> /
# <blockingconcurrentqueue.h>. The package exposes a `concurrentqueue` target,
# but its headers sit in a `moodycamel/` subdir there; re-expose them flat under
# the historical `moodycamel` target name the sources include by.
CPMAddPackage(
    NAME               concurrentqueue
    GITHUB_REPOSITORY  cameron314/concurrentqueue
    VERSION            1.0.5
)
add_library(moodycamel INTERFACE)
target_include_directories(moodycamel INTERFACE ${concurrentqueue_SOURCE_DIR})

if(RPCPP_BUILD_TESTS)
    enable_testing()
    CPMAddPackage(
        NAME               Catch2
        GITHUB_REPOSITORY  catchorg/Catch2
        VERSION            3.9.1
    )
    list(APPEND CMAKE_MODULE_PATH ${Catch2_SOURCE_DIR}/extras)
endif()

# QuickJS engine: only fetched for rpcpp's own tests/examples. Consumers that
# already embed quickjs-ng (e.g. via txiki.js) define the `qjs` target
# themselves before add_subdirectory(rpcpp); the guard below then skips the
# fetch and rpcpp compiles its header-only codec against that target.
if(RPCPP_BUILD_QUICKJS AND NOT TARGET qjs)
    CPMAddPackage(
        NAME               quickjs
        GITHUB_REPOSITORY  quickjs-ng/quickjs
        VERSION            0.13.0
    )
endif()

add_library(rpcpp INTERFACE)

target_sources(rpcpp
    INTERFACE
        FILE_SET HEADERS
        BASE_DIRS src
        FILES
            src/Codec.h
            src/Framer.h
            src/Resolver.h
            src/RpcEnvelope.h
            src/RpcServer.h
            src/RpcStdio.h
            src/Transport.h
            src/TypedRpcServer.h
            src/codecs/JsonCodec.h
            src/codecs/MsgpackCodec.h
            src/codecs/QuickJSCodec.h
            src/transports/QueueTransport.h
            src/transports/StdioTransport.h
            src/transports/QuickJSTransport.h
            src/qjs/Reader.hpp
            src/qjs/Writer.hpp
            src/qjs/Parser.hpp
            src/qjs/read.hpp
            src/qjs/write.hpp
)

target_include_directories(rpcpp INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)

target_compile_features(rpcpp INTERFACE cxx_std_20)

target_link_libraries(rpcpp
    INTERFACE
        reflectcpp::reflectcpp
        moodycamel
)

# When rpcpp itself fetched quickjs-ng, propagate it so tests/examples get
# quickjs.h on their include path. Consumers supplying their own `qjs` target
# (RPCPP_BUILD_QUICKJS=OFF) link it on their own side instead.
if(RPCPP_BUILD_QUICKJS AND TARGET qjs)
    target_link_libraries(rpcpp INTERFACE qjs)
endif()

add_library(rpcpp::rpcpp ALIAS rpcpp)

if(RPCPP_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(RPCPP_BUILD_TESTS)
    add_subdirectory(tests)
endif()
