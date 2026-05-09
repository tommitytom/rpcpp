add_executable(example_echo echo.cpp)
target_link_libraries(example_echo PRIVATE rpcpp::rpcpp)

add_executable(example_calculator calculator.cpp)
target_link_libraries(example_calculator PRIVATE rpcpp::rpcpp)

add_executable(example_discovery discovery.cpp)
target_link_libraries(example_discovery PRIVATE rpcpp::rpcpp)

if(RPCPP_BUILD_MSGPACK)
    add_executable(example_calculator_msgpack calculator_msgpack.cpp)
    target_link_libraries(example_calculator_msgpack PRIVATE rpcpp::rpcpp)
endif()
