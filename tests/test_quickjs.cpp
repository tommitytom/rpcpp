#include <chrono>
#include <optional>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include <quickjs.h>

#include <rfl.hpp>
#include <rfl/Generic.hpp>

#include "Resolver.h"
#include "RpcEnvelope.h"
#include "TypedRpcServer.h"
#include "codecs/QuickJSCodec.h"
#include "qjs/read.hpp"
#include "qjs/write.hpp"
#include "transports/QuickJSTransport.h"

namespace {

class Calculator {
public:
    int add(int a, int b) { return a + b; }

    // Async: resolves from a worker thread, exercising the deferred-output seam.
    void slow_add(int a, int b, rpcpp::Resolver<int> r) {
        std::thread([a, b, r]() mutable {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            r.resolve(a + b);
        }).detach();
    }
};

// Build a JSON-RPC request as a live JSValue by marshalling an RpcRequest
// through the QuickJS writer — the same path the codec uses.
JSValue make_request(JSContext* ctx, const std::string& method, int id,
                     int a, int b) {
    rpcpp::RpcRequest req{
        .method  = method,
        .id      = rfl::Generic{static_cast<std::int64_t>(id)},
        .params  = rfl::Generic{rfl::Generic::Array{
                       rfl::Generic{static_cast<std::int64_t>(a)},
                       rfl::Generic{static_cast<std::int64_t>(b)},
                   }},
        .jsonrpc = "2.0",
    };
    return rpcpp::qjs::write(ctx, req);
}

}  // namespace

TEST_CASE("QuickJS codec: synchronous method returns a JSValue response", "[quickjs]") {
    JSRuntime* rt = JS_NewRuntime();
    JSContext* ctx = JS_NewContext(rt);

    {
        Calculator calc;
        rpcpp::QuickJSTransport transport(ctx, [](JSContext*, JSValue) {});
        rpcpp::TypedRpcServer<Calculator, rpcpp::QuickJSCodec> server(
            calc, transport, rpcpp::QuickJSCodec{ctx});
        server.addMethod<&Calculator::add>();

        JSValue req = make_request(ctx, "add", 7, 2, 3);
        auto out = server.processMessage(req);
        JS_FreeValue(ctx, req);

        REQUIRE(out.has_value());

        // Sync path: materialize directly on the JS thread.
        JSValue resp = out->materialize(ctx);
        auto decoded = rpcpp::qjs::read<rpcpp::RpcResponse<int>>(ctx, resp);
        REQUIRE(decoded);
        REQUIRE(decoded.value().result == 5);
        REQUIRE(decoded.value().jsonrpc == "2.0");
        JS_FreeValue(ctx, resp);
    }

    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);  // asserts on leaked JSValues
}

TEST_CASE("QuickJS codec: unknown method yields an error envelope", "[quickjs]") {
    JSRuntime* rt = JS_NewRuntime();
    JSContext* ctx = JS_NewContext(rt);

    {
        Calculator calc;
        rpcpp::QuickJSTransport transport(ctx, [](JSContext*, JSValue) {});
        rpcpp::TypedRpcServer<Calculator, rpcpp::QuickJSCodec> server(
            calc, transport, rpcpp::QuickJSCodec{ctx});
        server.addMethod<&Calculator::add>();

        JSValue req = make_request(ctx, "nope", 1, 2, 3);
        auto out = server.processMessage(req);
        JS_FreeValue(ctx, req);

        REQUIRE(out.has_value());
        JSValue resp = out->materialize(ctx);
        auto decoded = rpcpp::qjs::read<rpcpp::RpcError>(ctx, resp);
        REQUIRE(decoded);
        REQUIRE(decoded.value().error.code == -32601);
        JS_FreeValue(ctx, resp);
    }

    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
}

TEST_CASE("QuickJS codec: async response materializes on the JS thread at drain",
          "[quickjs]") {
    JSRuntime* rt = JS_NewRuntime();
    JSContext* ctx = JS_NewContext(rt);

    {
        Calculator calc;

        std::optional<int> delivered;
        rpcpp::QuickJSTransport transport(ctx, [&](JSContext* c, JSValue v) {
            // Runs on the JS (test) thread inside drain().
            auto decoded = rpcpp::qjs::read<rpcpp::RpcResponse<int>>(c, v);
            if (decoded) delivered = decoded.value().result;
        });

        rpcpp::TypedRpcServer<Calculator, rpcpp::QuickJSCodec> server(
            calc, transport, rpcpp::QuickJSCodec{ctx});
        server.addAsyncMethod<&Calculator::slow_add>();

        JSValue req = make_request(ctx, "slow_add", 9, 20, 22);
        auto sync = server.processMessage(req);
        JS_FreeValue(ctx, req);

        // Async dispatch returns nothing synchronously.
        REQUIRE_FALSE(sync.has_value());

        // The worker resolves off-thread; the response sits as a deferred thunk
        // in the queue. Wait for all in-flight handlers, then materialize on the
        // JS thread.
        server.waitForInflight();
        REQUIRE_FALSE(delivered.has_value());  // nothing built before drain
        transport.drain();

        REQUIRE(delivered.has_value());
        REQUIRE(delivered.value() == 42);
    }

    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
}
