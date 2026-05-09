#include <cstdint>
#include <sstream>
#include <string>

#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>

#include <rfl.hpp>
#include <rfl/Generic.hpp>

#include "RpcEnvelope.h"
#include "TypedRpcServer.h"
#include "codecs/JsonCodec.h"

#ifdef RPCPP_HAS_MSGPACK
#include "codecs/MsgpackCodec.h"
#define RPCPP_TEST_CODECS rpcpp::JsonCodec, rpcpp::MsgpackCodec
#else
#define RPCPP_TEST_CODECS rpcpp::JsonCodec
#endif

namespace {

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

}  // namespace

TEMPLATE_TEST_CASE("processMessage handles a typed add()", "[codec]", RPCPP_TEST_CODECS) {
    using Codec = TestType;

    Calculator calc;
    rpcpp::TypedRpcServer<Calculator, Codec> server(calc);
    server.template addMethod<&Calculator::add>();

    auto req_bytes  = Codec::write(make_request("add", "x", 2, 3));
    auto resp_bytes = server.processMessage(
        typename Codec::input_t{req_bytes.data(), req_bytes.size()});

    auto resp = Codec::template read<rpcpp::RpcResponse<int>>(
        typename Codec::input_t{resp_bytes.data(), resp_bytes.size()});

    REQUIRE(resp.has_value());
    REQUIRE(resp.value().result  == 5);
    REQUIRE(resp.value().jsonrpc == "2.0");
}

TEMPLATE_TEST_CASE("unknown method returns -32601", "[codec]", RPCPP_TEST_CODECS) {
    using Codec = TestType;

    Calculator calc;
    rpcpp::TypedRpcServer<Calculator, Codec> server(calc);
    server.template addMethod<&Calculator::add>();

    auto req_bytes  = Codec::write(make_request("nope", "y", 1, 1));
    auto resp_bytes = server.processMessage(
        typename Codec::input_t{req_bytes.data(), req_bytes.size()});

    auto err = Codec::template read<rpcpp::RpcError>(
        typename Codec::input_t{resp_bytes.data(), resp_bytes.size()});

    REQUIRE(err.has_value());
    REQUIRE(err.value().error.code == -32601);
}

TEMPLATE_TEST_CASE("run() dispatches a batch through stringstreams", "[codec][run]", RPCPP_TEST_CODECS) {
    using Codec = TestType;
    using InF   = typename Codec::default_in_framer;

    Calculator calc;
    rpcpp::TypedRpcServer<Calculator, Codec> server(calc);
    server.template addMethod<&Calculator::add>();
    server.template addMethod<&Calculator::multiply>();

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
    REQUIRE_FALSE(frame_c.has_value());

    auto resp_a = Codec::template read<rpcpp::RpcResponse<int>>(
        typename Codec::input_t{frame_a->data(), frame_a->size()});
    auto resp_b = Codec::template read<rpcpp::RpcResponse<int>>(
        typename Codec::input_t{frame_b->data(), frame_b->size()});

    REQUIRE(resp_a.has_value());
    REQUIRE(resp_a.value().result == 5);
    REQUIRE(resp_b.has_value());
    REQUIRE(resp_b.value().result == 20);
}
