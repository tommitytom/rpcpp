#include <chrono>
#include <cstdint>
#include <sstream>
#include <string>
#include <thread>

#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>

#include <rfl.hpp>
#include <rfl/Generic.hpp>

#include "Resolver.h"
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

    REQUIRE(resp_bytes.has_value());
    auto resp = Codec::template read<rpcpp::RpcResponse<int>>(
        typename Codec::input_t{resp_bytes->data(), resp_bytes->size()});

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

    REQUIRE(resp_bytes.has_value());
    auto err = Codec::template read<rpcpp::RpcError>(
        typename Codec::input_t{resp_bytes->data(), resp_bytes->size()});

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

namespace {

class AsyncService {
public:
    void slow_add(int a, int b, rpcpp::Resolver<int> r) {
        std::thread([a, b, r = std::move(r)]() mutable {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            r.resolve(a + b);
        }).detach();
    }

    void rejecter(int /*a*/, int /*b*/, rpcpp::Resolver<int> r) {
        r.reject(-12345, "nope");
    }

    void abandoner(int /*a*/, int /*b*/, rpcpp::Resolver<int> /*r*/) {
        // Resolver goes out of scope without resolve/reject being called.
    }
};

}  // namespace

TEMPLATE_TEST_CASE("typed async resolves on a worker thread", "[codec][async]", RPCPP_TEST_CODECS) {
    using Codec = TestType;
    using InF   = typename Codec::default_in_framer;

    AsyncService svc;
    rpcpp::TypedRpcServer<AsyncService, Codec> server(svc);
    server.template addAsyncMethod<&AsyncService::slow_add>();

    std::stringstream ss_in;
    std::stringstream ss_out;
    InF::write(ss_in, Codec::write(make_request("slow_add", "1", 2, 3)));

    server.run(ss_in, ss_out);

    auto frame = InF::template read<typename Codec::buffer_t>(ss_out);
    REQUIRE(frame.has_value());
    auto resp = Codec::template read<rpcpp::RpcResponse<int>>(
        typename Codec::input_t{frame->data(), frame->size()});
    REQUIRE(resp.has_value());
    REQUIRE(resp.value().result == 5);
}

TEMPLATE_TEST_CASE("typed async reject surfaces a custom error envelope", "[codec][async]", RPCPP_TEST_CODECS) {
    using Codec = TestType;
    using InF   = typename Codec::default_in_framer;

    AsyncService svc;
    rpcpp::TypedRpcServer<AsyncService, Codec> server(svc);
    server.template addAsyncMethod<&AsyncService::rejecter>();

    std::stringstream ss_in;
    std::stringstream ss_out;
    InF::write(ss_in, Codec::write(make_request("rejecter", "1", 1, 1)));
    server.run(ss_in, ss_out);

    auto frame = InF::template read<typename Codec::buffer_t>(ss_out);
    REQUIRE(frame.has_value());
    auto err = Codec::template read<rpcpp::RpcError>(
        typename Codec::input_t{frame->data(), frame->size()});
    REQUIRE(err.has_value());
    REQUIRE(err.value().error.code == -12345);
    REQUIRE(err.value().error.message == "nope");
}

TEMPLATE_TEST_CASE("dropped resolver auto-rejects -32603", "[codec][async]", RPCPP_TEST_CODECS) {
    using Codec = TestType;
    using InF   = typename Codec::default_in_framer;

    AsyncService svc;
    rpcpp::TypedRpcServer<AsyncService, Codec> server(svc);
    server.template addAsyncMethod<&AsyncService::abandoner>();

    std::stringstream ss_in;
    std::stringstream ss_out;
    InF::write(ss_in, Codec::write(make_request("abandoner", "1", 0, 0)));
    server.run(ss_in, ss_out);

    auto frame = InF::template read<typename Codec::buffer_t>(ss_out);
    REQUIRE(frame.has_value());
    auto err = Codec::template read<rpcpp::RpcError>(
        typename Codec::input_t{frame->data(), frame->size()});
    REQUIRE(err.has_value());
    REQUIRE(err.value().error.code == -32603);
}

TEST_CASE("raw addAsyncHandler reaches the wire", "[codec][async][raw]") {
    using Codec = rpcpp::JsonCodec;
    using InF   = typename Codec::default_in_framer;

    rpcpp::RpcServer<Codec> server;
    server.addAsyncHandler("ping",
        [](rfl::Generic id, rfl::Generic /*params*/, rpcpp::AsyncContext<Codec> ctx) {
            std::thread([id = std::move(id), ctx]() mutable {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                ctx.respondBytes(Codec::write(rpcpp::RpcResponse<std::string>{
                    .id     = std::move(id),
                    .result = "pong",
                }));
            }).detach();
        });

    std::stringstream ss_in;
    std::stringstream ss_out;
    rpcpp::RpcRequest req{
        .method  = "ping",
        .id      = rfl::Generic{std::string{"1"}},
        .params  = rfl::Generic{},
        .jsonrpc = "2.0",
    };
    InF::write(ss_in, Codec::write(req));
    server.run(ss_in, ss_out);

    auto frame = InF::template read<typename Codec::buffer_t>(ss_out);
    REQUIRE(frame.has_value());
    auto resp = Codec::template read<rpcpp::RpcResponse<std::string>>(
        typename Codec::input_t{frame->data(), frame->size()});
    REQUIRE(resp.has_value());
    REQUIRE(resp.value().result == "pong");
}
