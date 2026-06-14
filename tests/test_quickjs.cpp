#include <chrono>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <thread>
#include <vector>

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

// =============================================================================
// Fixtures
// =============================================================================

namespace {

struct Point {
    int x;
    int y;
    std::string label;
};

struct Inner {
    int v;
};

struct Outer {
    std::optional<Inner> inner;
};

class Svc {
public:
    int add(int a, int b)                       { return a + b; }
    int sum5(int a, int b, int c, int d, int e) { return a + b + c + d + e; }
    void noop()                                 {}
    std::string greet(std::string who)          { return "hi " + who; }
    Point makePoint(int x, int y)               { return {x, y, "p"}; }
    Point scale(Point p, double f) {
        return {static_cast<int>(p.x * f), static_cast<int>(p.y * f), p.label};
    }

    // Async: resolves from a worker thread (deferred-output seam).
    void slow_add(int a, int b, rpcpp::Resolver<int> r) {
        std::thread([a, b, r]() mutable {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            r.resolve(a + b);
        }).detach();
    }
    // Async: rejects synchronously with a caller-supplied code.
    void reject_now(int code, rpcpp::Resolver<int> r) { r.reject(code, "nope"); }
    // Async: drops the resolver without resolving → auto -32603.
    void abandon(rpcpp::Resolver<int>) {}
    // Async: throws in the body before resolving → -32603.
    void throw_async(int, rpcpp::Resolver<int>) { throw std::runtime_error("async boom"); }
    // Sync: throws → -32603.
    void throw_now() { throw std::runtime_error("boom"); }
};

// Build a request as a live JSValue by marshalling an RpcRequest (the codec's
// own path). `id`/`params` are rfl::Generic, matching the envelope.
JSValue write_request(JSContext* ctx, std::string method,
                      std::optional<rfl::Generic> id, rfl::Generic params,
                      std::string jsonrpc = "2.0") {
    return rpcpp::qjs::write(ctx, rpcpp::RpcRequest{
        .method  = std::move(method),
        .id      = std::move(id),
        .params  = std::move(params),
        .jsonrpc = std::move(jsonrpc),
    });
}

rfl::Generic gint(std::int64_t v) { return rfl::Generic{v}; }

rfl::Generic int_args(std::initializer_list<std::int64_t> xs) {
    rfl::Generic::Array a;
    for (auto x : xs) a.push_back(rfl::Generic{x});
    return rfl::Generic{a};
}

rfl::Generic point_arg(int x, int y, std::string label) {
    rfl::Generic::Object o;
    o["x"]     = rfl::Generic{static_cast<std::int64_t>(x)};
    o["y"]     = rfl::Generic{static_cast<std::int64_t>(y)};
    o["label"] = rfl::Generic{std::move(label)};
    return rfl::Generic{o};
}

// RAII runtime+context so every TEST_CASE gets a fresh, leak-checked engine.
struct Rt {
    JSRuntime* rt  = JS_NewRuntime();
    JSContext* ctx = JS_NewContext(rt);
    ~Rt() { JS_FreeContext(ctx); JS_FreeRuntime(rt); }  // asserts on leaked refs
};

}  // namespace

// =============================================================================
// A1 — Reader/Writer format round-trips (exercise to_basic_type / from_basic_type
//       and the refcount pool directly; all under ASan via ~Rt()).
// =============================================================================

TEST_CASE("qjs format: nested struct round-trip", "[quickjs][format]") {
    Rt e;
    Point p{3, 4, "origin"};
    JSValue v = rpcpp::qjs::write(e.ctx, p);
    auto back = rpcpp::qjs::read<Point>(e.ctx, v);
    REQUIRE(back);
    REQUIRE(back.value().x == 3);
    REQUIRE(back.value().y == 4);
    REQUIRE(back.value().label == "origin");
    JS_FreeValue(e.ctx, v);
}

TEST_CASE("qjs format: vector<struct> and empty vector", "[quickjs][format]") {
    Rt e;
    std::vector<Point> pts{{1, 2, "a"}, {5, 6, "b"}};
    JSValue v = rpcpp::qjs::write(e.ctx, pts);
    auto back = rpcpp::qjs::read<std::vector<Point>>(e.ctx, v);
    REQUIRE(back);
    REQUIRE(back.value().size() == 2);
    REQUIRE(back.value()[1].label == "b");
    JS_FreeValue(e.ctx, v);

    std::vector<Point> empty;
    JSValue ve = rpcpp::qjs::write(e.ctx, empty);
    auto be = rpcpp::qjs::read<std::vector<Point>>(e.ctx, ve);
    REQUIRE(be);
    REQUIRE(be.value().empty());
    JS_FreeValue(e.ctx, ve);
}

TEST_CASE("qjs format: optional present and absent", "[quickjs][format]") {
    Rt e;
    Outer present{Inner{7}};
    JSValue vp = rpcpp::qjs::write(e.ctx, present);
    auto bp = rpcpp::qjs::read<Outer>(e.ctx, vp);
    REQUIRE(bp);
    REQUIRE(bp.value().inner.has_value());
    REQUIRE(bp.value().inner->v == 7);
    JS_FreeValue(e.ctx, vp);

    Outer absent{std::nullopt};
    JSValue va = rpcpp::qjs::write(e.ctx, absent);
    auto ba = rpcpp::qjs::read<Outer>(e.ctx, va);
    REQUIRE(ba);
    REQUIRE_FALSE(ba.value().inner.has_value());
    JS_FreeValue(e.ctx, va);
}

TEST_CASE("qjs format: strings — empty, UTF-8, embedded NUL", "[quickjs][format]") {
    Rt e;
    for (const std::string& s : {std::string{}, std::string{"héllo ☃"},
                                 std::string("a\0b", 3)}) {
        JSValue v = rpcpp::qjs::write(e.ctx, Point{0, 0, s});
        auto back = rpcpp::qjs::read<Point>(e.ctx, v);
        REQUIRE(back);
        REQUIRE(back.value().label == s);
        JS_FreeValue(e.ctx, v);
    }
}

TEST_CASE("qjs format: error paths free cleanly (no leak)", "[quickjs][format]") {
    Rt e;
    // struct from a non-object
    {
        JSValue n = JS_NewInt32(e.ctx, 5);
        auto r = rpcpp::qjs::read<Point>(e.ctx, n);
        REQUIRE_FALSE(r);
        JS_FreeValue(e.ctx, n);
    }
    // array from a non-array
    {
        JSValue n = JS_NewInt32(e.ctx, 5);
        auto r = rpcpp::qjs::read<std::vector<int>>(e.ctx, n);
        REQUIRE_FALSE(r);
        JS_FreeValue(e.ctx, n);
    }
    // object missing a required field
    {
        JSValue o = JS_NewObject(e.ctx);
        JS_SetPropertyStr(e.ctx, o, "x", JS_NewInt32(e.ctx, 1));  // y, label missing
        auto r = rpcpp::qjs::read<Point>(e.ctx, o);
        REQUIRE_FALSE(r);
        JS_FreeValue(e.ctx, o);
    }
    // array element of the wrong type mid-stream
    {
        JSValue arr = JS_NewArray(e.ctx);
        JSValue good = JS_NewObject(e.ctx);
        JS_SetPropertyStr(e.ctx, good, "v", JS_NewInt32(e.ctx, 1));
        JS_SetPropertyUint32(e.ctx, arr, 0, good);
        JS_SetPropertyUint32(e.ctx, arr, 1, JS_NewInt32(e.ctx, 99));  // not an object
        auto r = rpcpp::qjs::read<std::vector<Inner>>(e.ctx, arr);
        REQUIRE_FALSE(r);
        JS_FreeValue(e.ctx, arr);
    }
}

TEST_CASE("qjs format: bool is strict — a number is not a bool", "[quickjs][format]") {
    Rt e;
    struct B { bool b; };
    JSValue o = JS_NewObject(e.ctx);
    JS_SetPropertyStr(e.ctx, o, "b", JS_NewInt32(e.ctx, 1));  // number, not bool
    auto r = rpcpp::qjs::read<B>(e.ctx, o);
    REQUIRE_FALSE(r);
    JS_FreeValue(e.ctx, o);
}

// --- Pinned quirks (current behavior; TODO(harden) candidates) ---------------

TEST_CASE("qjs format: int64 > 2^53 loses precision", "[quickjs][format][quirk]") {
    Rt e;
    // TODO(harden): QuickJS numbers are IEEE doubles, so JS_NewInt64 demotes
    // values that don't fit int32 to float64. 2^53+1 is unrepresentable and
    // rounds to 2^53. A BigInt path would preserve it. Pinning current behavior.
    struct Big { std::int64_t v; };
    JSValue w = rpcpp::qjs::write(e.ctx, Big{(1LL << 53) + 1});
    auto back = rpcpp::qjs::read<Big>(e.ctx, w);
    REQUIRE(back);
    REQUIRE(back.value().v == (1LL << 53));  // not (1<<53)+1
    JS_FreeValue(e.ctx, w);
}

TEST_CASE("qjs format: negative read into unsigned wraps", "[quickjs][format][quirk]") {
    Rt e;
    // TODO(harden): no range validation — a negative JS number read into an
    // unsigned field wraps (static_cast). Pinning current behavior.
    struct SInt { std::int32_t v; };
    struct UInt { std::uint32_t v; };
    JSValue w = rpcpp::qjs::write(e.ctx, SInt{-1});
    auto back = rpcpp::qjs::read<UInt>(e.ctx, w);
    REQUIRE(back);
    REQUIRE(back.value().v == 4294967295u);
    JS_FreeValue(e.ctx, w);
}

TEST_CASE("qjs format: NaN/Inf pass through", "[quickjs][format][quirk]") {
    Rt e;
    // TODO(harden): non-finite doubles are not JSON-compliant but propagate
    // through the JS number layer unchanged. Pinning current behavior.
    struct D { double d; };
    JSValue wn = rpcpp::qjs::write(e.ctx, D{std::nan("")});
    auto bn = rpcpp::qjs::read<D>(e.ctx, wn);
    REQUIRE(bn);
    REQUIRE(std::isnan(bn.value().d));
    JS_FreeValue(e.ctx, wn);

    JSValue wi = rpcpp::qjs::write(e.ctx, D{INFINITY});
    auto bi = rpcpp::qjs::read<D>(e.ctx, wi);
    REQUIRE(bi);
    REQUIRE(std::isinf(bi.value().d));
    JS_FreeValue(e.ctx, wi);
}

// =============================================================================
// A2 — Server dispatch over the QuickJS codec.
// =============================================================================

namespace {

// Convenience: run one sync request and decode the response as T.
template <class T>
rfl::Result<T> call_sync(JSContext* ctx, rpcpp::TypedRpcServer<Svc, rpcpp::QuickJSCodec>& server,
                         JSValue req) {
    auto out = server.processMessage(req);
    if (!out) return rfl::error("no response");
    JSValue resp = out->materialize(ctx);
    auto r = rpcpp::qjs::read<T>(ctx, resp);
    JS_FreeValue(ctx, resp);
    return r;
}

}  // namespace

TEST_CASE("qjs server: sync add returns 5", "[quickjs][server]") {
    Rt e;
    Svc svc;
    rpcpp::QuickJSTransport transport(e.ctx, [](JSContext*, JSValue) {});
    rpcpp::TypedRpcServer<Svc, rpcpp::QuickJSCodec> server(svc, transport, rpcpp::QuickJSCodec{e.ctx});
    server.addMethod<&Svc::add>();

    JSValue req = write_request(e.ctx, "add", gint(7), int_args({2, 3}));
    auto resp = call_sync<rpcpp::RpcResponse<int>>(e.ctx, server, req);
    JS_FreeValue(e.ctx, req);
    REQUIRE(resp);
    REQUIRE(resp.value().result == 5);
    REQUIRE(resp.value().jsonrpc == "2.0");
}

TEST_CASE("qjs server: multi-arg, void, string, and struct methods", "[quickjs][server]") {
    Rt e;
    Svc svc;
    rpcpp::QuickJSTransport transport(e.ctx, [](JSContext*, JSValue) {});
    rpcpp::TypedRpcServer<Svc, rpcpp::QuickJSCodec> server(svc, transport, rpcpp::QuickJSCodec{e.ctx});
    server.addMethod<&Svc::sum5>();
    server.addMethod<&Svc::noop>();
    server.addMethod<&Svc::greet>();
    server.addMethod<&Svc::makePoint>();
    server.addMethod<&Svc::scale>();

    SECTION("multi-arg (5 params)") {
        JSValue req = write_request(e.ctx, "sum5", gint(1), int_args({1, 2, 3, 4, 5}));
        auto r = call_sync<rpcpp::RpcResponse<int>>(e.ctx, server, req);
        JS_FreeValue(e.ctx, req);
        REQUIRE(r);
        REQUIRE(r.value().result == 15);
    }
    SECTION("void return decodes as null result") {
        JSValue req = write_request(e.ctx, "noop", gint(1), int_args({}));
        auto r = call_sync<rpcpp::RpcResponse<std::optional<int>>>(e.ctx, server, req);
        JS_FreeValue(e.ctx, req);
        REQUIRE(r);
        REQUIRE_FALSE(r.value().result.has_value());
    }
    SECTION("string in/out") {
        rfl::Generic::Array a; a.push_back(rfl::Generic{std::string{"bob"}});
        JSValue req = write_request(e.ctx, "greet", gint(1), rfl::Generic{a});
        auto r = call_sync<rpcpp::RpcResponse<std::string>>(e.ctx, server, req);
        JS_FreeValue(e.ctx, req);
        REQUIRE(r);
        REQUIRE(r.value().result == "hi bob");
    }
    SECTION("struct result") {
        JSValue req = write_request(e.ctx, "makePoint", gint(1), int_args({2, 9}));
        auto r = call_sync<rpcpp::RpcResponse<Point>>(e.ctx, server, req);
        JS_FreeValue(e.ctx, req);
        REQUIRE(r);
        REQUIRE(r.value().result.x == 2);
        REQUIRE(r.value().result.y == 9);
    }
    SECTION("struct param") {
        rfl::Generic::Array a;
        a.push_back(point_arg(3, 4, "q"));
        a.push_back(rfl::Generic{2.0});
        JSValue req = write_request(e.ctx, "scale", gint(1), rfl::Generic{a});
        auto r = call_sync<rpcpp::RpcResponse<Point>>(e.ctx, server, req);
        JS_FreeValue(e.ctx, req);
        REQUIRE(r);
        REQUIRE(r.value().result.x == 6);
        REQUIRE(r.value().result.y == 8);
        REQUIRE(r.value().result.label == "q");
    }
}

TEST_CASE("qjs server: error codes", "[quickjs][server][error]") {
    Rt e;
    Svc svc;
    rpcpp::QuickJSTransport transport(e.ctx, [](JSContext*, JSValue) {});
    rpcpp::TypedRpcServer<Svc, rpcpp::QuickJSCodec> server(svc, transport, rpcpp::QuickJSCodec{e.ctx});
    server.addMethod<&Svc::add>();
    server.addMethod<&Svc::throw_now>();
    // (-32602 invalid-params lives in the async section — sync bad params throw
    //  and surface as -32603, while the async path reports -32602 explicitly.)

    SECTION("-32601 method not found") {
        JSValue req = write_request(e.ctx, "nope", gint(1), int_args({1, 2}));
        auto r = call_sync<rpcpp::RpcError>(e.ctx, server, req);
        JS_FreeValue(e.ctx, req);
        REQUIRE(r);
        REQUIRE(r.value().error.code == -32601);
    }
    SECTION("-32600 invalid jsonrpc version") {
        JSValue req = write_request(e.ctx, "add", gint(1), int_args({1, 2}), "1.0");
        auto r = call_sync<rpcpp::RpcError>(e.ctx, server, req);
        JS_FreeValue(e.ctx, req);
        REQUIRE(r);
        REQUIRE(r.value().error.code == -32600);
    }
    SECTION("-32603 sync handler throws") {
        JSValue req = write_request(e.ctx, "throw_now", gint(1), int_args({}));
        auto r = call_sync<rpcpp::RpcError>(e.ctx, server, req);
        JS_FreeValue(e.ctx, req);
        REQUIRE(r);
        REQUIRE(r.value().error.code == -32603);
    }
    SECTION("-32700 request is not an object") {
        JSValue n = JS_NewInt32(e.ctx, 5);
        auto r = call_sync<rpcpp::RpcError>(e.ctx, server, n);
        JS_FreeValue(e.ctx, n);
        REQUIRE(r);
        REQUIRE(r.value().error.code == -32700);
    }
}

TEST_CASE("qjs server: request without id still replies (null id)", "[quickjs][server]") {
    Rt e;
    Svc svc;
    rpcpp::QuickJSTransport transport(e.ctx, [](JSContext*, JSValue) {});
    rpcpp::TypedRpcServer<Svc, rpcpp::QuickJSCodec> server(svc, transport, rpcpp::QuickJSCodec{e.ctx});
    server.addMethod<&Svc::add>();
    // TODO(harden): rpcpp replies to id-less requests with a null id rather than
    // suppressing the response. Pinning current behavior.
    JSValue req = write_request(e.ctx, "add", std::nullopt, int_args({2, 3}));
    auto r = call_sync<rpcpp::RpcResponse<int>>(e.ctx, server, req);
    JS_FreeValue(e.ctx, req);
    REQUIRE(r);
    REQUIRE(r.value().result == 5);
}

TEST_CASE("qjs server: discovery returns a schema object", "[quickjs][server]") {
    Rt e;
    Svc svc;
    rpcpp::QuickJSTransport transport(e.ctx, [](JSContext*, JSValue) {});
    rpcpp::TypedRpcServer<Svc, rpcpp::QuickJSCodec> server(svc, transport, rpcpp::QuickJSCodec{e.ctx});
    server.addMethod<&Svc::add>();
    server.addDiscoveryMethod();

    JSValue req = write_request(e.ctx, "rpc.discover", gint(1), int_args({}));
    auto r = call_sync<rpcpp::RpcResponse<rfl::Generic::Object>>(e.ctx, server, req);
    JS_FreeValue(e.ctx, req);
    REQUIRE(r);
    bool has_methods = false;
    for (const auto& [k, v] : r.value().result) {
        if (k == "methods") has_methods = true;
    }
    REQUIRE(has_methods);
}

// =============================================================================
// Async dispatch + the thread-safe deferred-output seam.
// =============================================================================

namespace {

// Drive one async request and return the single drained response, decoded as T.
template <class T>
rfl::Result<T> call_async(JSContext* ctx, Svc& svc,
                          void (*reg)(rpcpp::TypedRpcServer<Svc, rpcpp::QuickJSCodec>&),
                          std::string method, rfl::Generic params) {
    rfl::Result<T> captured = rfl::error("nothing drained");
    rpcpp::QuickJSTransport transport(ctx, [&](JSContext* c, JSValue v) {
        captured = rpcpp::qjs::read<T>(c, v);
    });
    rpcpp::TypedRpcServer<Svc, rpcpp::QuickJSCodec> server(svc, transport, rpcpp::QuickJSCodec{ctx});
    reg(server);

    JSValue req = write_request(ctx, std::move(method), rfl::Generic{std::int64_t{1}}, std::move(params));
    auto sync = server.processMessage(req);
    JS_FreeValue(ctx, req);
    REQUIRE_FALSE(sync.has_value());      // async → nothing synchronous
    server.waitForInflight();
    transport.drain();                     // materialize on the JS thread
    return captured;
}

}  // namespace

TEST_CASE("qjs async: resolve materializes at drain", "[quickjs][server][async]") {
    Rt e;
    Svc svc;
    auto r = call_async<rpcpp::RpcResponse<int>>(
        e.ctx, svc, [](auto& s) { s.template addAsyncMethod<&Svc::slow_add>(); },
        "slow_add", int_args({20, 22}));
    REQUIRE(r);
    REQUIRE(r.value().result == 42);
}

TEST_CASE("qjs async: custom reject code", "[quickjs][server][async]") {
    Rt e;
    Svc svc;
    auto r = call_async<rpcpp::RpcError>(
        e.ctx, svc, [](auto& s) { s.template addAsyncMethod<&Svc::reject_now>(); },
        "reject_now", int_args({-12345}));
    REQUIRE(r);
    REQUIRE(r.value().error.code == -12345);
    REQUIRE(r.value().error.message == "nope");
}

TEST_CASE("qjs async: abandoned resolver auto-rejects -32603", "[quickjs][server][async]") {
    Rt e;
    Svc svc;
    auto r = call_async<rpcpp::RpcError>(
        e.ctx, svc, [](auto& s) { s.template addAsyncMethod<&Svc::abandon>(); },
        "abandon", int_args({}));
    REQUIRE(r);
    REQUIRE(r.value().error.code == -32603);
}

TEST_CASE("qjs async: throw in body -> -32603", "[quickjs][server][async]") {
    Rt e;
    Svc svc;
    auto r = call_async<rpcpp::RpcError>(
        e.ctx, svc, [](auto& s) { s.template addAsyncMethod<&Svc::throw_async>(); },
        "throw_async", int_args({1}));
    REQUIRE(r);
    REQUIRE(r.value().error.code == -32603);
}

TEST_CASE("qjs async: invalid params on async path -> -32602", "[quickjs][server][async]") {
    Rt e;
    Svc svc;
    auto r = call_async<rpcpp::RpcError>(
        e.ctx, svc, [](auto& s) { s.template addAsyncMethod<&Svc::slow_add>(); },
        "slow_add", int_args({1}));  // wrong arity
    REQUIRE(r);
    REQUIRE(r.value().error.code == -32602);
}

TEST_CASE("qjs transport: drain on empty queue and double-drain are no-ops",
          "[quickjs][transport]") {
    Rt e;
    int calls = 0;
    rpcpp::QuickJSTransport transport(e.ctx, [&](JSContext*, JSValue) { ++calls; });
    transport.drain();
    transport.drain();
    REQUIRE(calls == 0);
}
