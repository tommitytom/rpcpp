include(Catch)

add_executable(test_framer test_framer.cpp)
target_link_libraries(test_framer PRIVATE rpcpp::rpcpp Catch2::Catch2WithMain)
catch_discover_tests(test_framer)

add_executable(test_codec test_codec.cpp)
target_link_libraries(test_codec PRIVATE rpcpp::rpcpp Catch2::Catch2WithMain)
if(RPCPP_BUILD_MSGPACK)
    target_compile_definitions(test_codec PRIVATE RPCPP_HAS_MSGPACK)
endif()
catch_discover_tests(test_codec)

if(RPCPP_BUILD_QUICKJS)
    add_executable(test_quickjs test_quickjs.cpp)
    target_link_libraries(test_quickjs PRIVATE rpcpp::rpcpp Catch2::Catch2WithMain)
    catch_discover_tests(test_quickjs)
endif()
