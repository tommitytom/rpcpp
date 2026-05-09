add_executable(test_framer test_framer.cpp)
target_link_libraries(test_framer PRIVATE rpcpp::rpcpp)

add_executable(test_codec test_codec.cpp)
target_link_libraries(test_codec PRIVATE rpcpp::rpcpp)
if(RPCPP_BUILD_MSGPACK)
    target_compile_definitions(test_codec PRIVATE RPCPP_HAS_MSGPACK)
endif()

include(CTest)
add_test(NAME framer COMMAND test_framer)
add_test(NAME codec  COMMAND test_codec)
