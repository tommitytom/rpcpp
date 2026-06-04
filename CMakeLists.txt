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

if(RPCPP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(deps/catch2)
    list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/deps/catch2/extras)
endif()

if(RPCPP_BUILD_MSGPACK)
    set(REFLECTCPP_MSGPACK ON CACHE BOOL "" FORCE)
endif()

add_subdirectory(deps/reflect-cpp)

add_library(moodycamel INTERFACE)
target_include_directories(moodycamel INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/deps/moodycamel
)

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
            src/transports/QueueTransport.h
            src/transports/StdioTransport.h
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

add_library(rpcpp::rpcpp ALIAS rpcpp)

if(RPCPP_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(RPCPP_BUILD_TESTS)
    add_subdirectory(tests)
endif()
