add_executable(test_msgpack_smoke msgpack_smoke.cpp)
target_link_libraries(test_msgpack_smoke PRIVATE rpcpp::rpcpp)
