// Cross-platform codec dispatch tests (no fork, no real I/O).
// Exercises both processMessage() directly and the full run() loop via
// std::stringstream. Runs against JsonCodec always; against MsgpackCodec
// when RPCPP_HAS_MSGPACK is defined.

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <rfl.hpp>
#include <rfl/Generic.hpp>

#include "RpcEnvelope.h"
#include "TypedRpcServer.h"
#include "codecs/JsonCodec.h"

#ifdef RPCPP_HAS_MSGPACK
#include "codecs/MsgpackCodec.h"
#endif

namespace {

#define REQUIRE(cond)                                                       \
    do {                                                                    \
        if (!(cond)) {                                                      \
            std::cerr << "FAIL: " #cond " at " << __FILE__ << ':'           \
                      << __LINE__ << '\n';                                  \
            std::exit(1);                                                   \
        }                                                                   \
    } while (0)

class Calculator {
public:
    int add(int a, int b)      { return a + b; }
    int multiply(int a, int b) { return a * b; }
};

rpcpp::RpcRequest make_request(std::string method, std::string id,
                               std::int64_t a, std::int64_t b) {
    return rpcpp::RpcRequest{
        .method  = std::move(method),
        .id      = rfl::Generic{std::move(id)},
        .params  = rfl::Generic{rfl::Generic::Array{
                       rfl::Generic{a}, rfl::Generic{b},
                   }},
        .jsonrpc = "2.0",
    };
}

template <class Codec>
void test_process_message() {
    Calculator calc;
    rpcpp::TypedRpcServer<Calculator, Codec> server(calc);
    server.template addMethod<&Calculator::add>();

    auto req_bytes = Codec::write(make_request("add", "x", 2, 3));
    auto resp_bytes = server.processMessage(
        typename Codec::input_t{req_bytes.data(), req_bytes.size()});

    auto resp_r = Codec::template read<rpcpp::RpcResponse<int>>(
        typename Codec::input_t{resp_bytes.data(), resp_bytes.size()});
    REQUIRE(resp_r.has_value());
    REQUIRE(resp_r.value().result == 5);
    REQUIRE(resp_r.value().jsonrpc == "2.0");
}

template <class Codec>
void test_method_not_found() {
    Calculator calc;
    rpcpp::TypedRpcServer<Calculator, Codec> server(calc);
    server.template addMethod<&Calculator::add>();

    auto req_bytes = Codec::write(make_request("nope", "y", 1, 1));
    auto resp_bytes = server.processMessage(
        typename Codec::input_t{req_bytes.data(), req_bytes.size()});

    auto err_r = Codec::template read<rpcpp::RpcError>(
        typename Codec::input_t{resp_bytes.data(), resp_bytes.size()});
    REQUIRE(err_r.has_value());
    REQUIRE(err_r.value().error.code == -32601);
}

template <class Codec>
void test_run_loop() {
    Calculator calc;
    rpcpp::TypedRpcServer<Calculator, Codec> server(calc);
    server.template addMethod<&Calculator::add>();
    server.template addMethod<&Calculator::multiply>();

    using InF  = typename Codec::default_in_framer;
    using OutF = typename Codec::default_out_framer;

    std::stringstream ss_in;
    std::stringstream ss_out;

    InF::write(ss_in, Codec::write(make_request("add",      "1", 2, 3)));
    InF::write(ss_in, Codec::write(make_request("multiply", "2", 4, 5)));

    server.run(ss_in, ss_out);

    auto frame_a = InF::template read<typename Codec::buffer_t>(ss_out);
    auto frame_b = InF::template read<typename Codec::buffer_t>(ss_out);
    auto frame_c = InF::template read<typename Codec::buffer_t>(ss_out);
    REQUIRE(frame_a.has_value());
    REQUIRE(frame_b.has_value());
    REQUIRE(!frame_c.has_value());

    auto resp_a = Codec::template read<rpcpp::RpcResponse<int>>(
        typename Codec::input_t{frame_a->data(), frame_a->size()});
    auto resp_b = Codec::template read<rpcpp::RpcResponse<int>>(
        typename Codec::input_t{frame_b->data(), frame_b->size()});
    REQUIRE(resp_a.has_value() && resp_a.value().result == 5);
    REQUIRE(resp_b.has_value() && resp_b.value().result == 20);
}

template <class Codec>
void run_suite(const char* name) {
    std::cout << "  " << name << ": process_message ";
    test_process_message<Codec>();
    std::cout << "ok, method_not_found ";
    test_method_not_found<Codec>();
    std::cout << "ok, run_loop ";
    test_run_loop<Codec>();
    std::cout << "ok\n";
}

}  // namespace

int main() {
    std::cout << "test_codec:\n";
    run_suite<rpcpp::JsonCodec>("JsonCodec");
#ifdef RPCPP_HAS_MSGPACK
    run_suite<rpcpp::MsgpackCodec>("MsgpackCodec");
#endif
    std::cout << "PASS\n";
    return 0;
}
