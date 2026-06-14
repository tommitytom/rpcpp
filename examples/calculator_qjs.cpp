// In-process example: the QuickJS codec marshals C++ <-> JSValue with no byte
// serialization. Here we drive the server with a request built as a live
// JSValue and print the decoded result — the shape an embedder (e.g. a JS
// runtime calling into native code) would use.

#include <cstdio>

#include <quickjs.h>

#include "TypedRpcServer.h"
#include "codecs/QuickJSCodec.h"
#include "qjs/read.hpp"
#include "qjs/write.hpp"
#include "transports/QuickJSTransport.h"

class Calculator {
public:
    int add(int a, int b)      { return a + b; }
    int multiply(int a, int b) { return a * b; }
};

int main() {
    JSRuntime* rt = JS_NewRuntime();
    JSContext* ctx = JS_NewContext(rt);

    Calculator calc;
    rpcpp::QuickJSTransport transport(ctx, [](JSContext*, JSValue) {});
    rpcpp::TypedRpcServer<Calculator, rpcpp::QuickJSCodec> server(
        calc, transport, rpcpp::QuickJSCodec{ctx});

    server.addMethod<&Calculator::add>();
    server.addMethod<&Calculator::multiply>();

    // Build {"jsonrpc":"2.0","id":1,"method":"multiply","params":[6,7]}.
    rpcpp::RpcRequest req{
        .method  = "multiply",
        .id      = rfl::Generic{static_cast<std::int64_t>(1)},
        .params  = rfl::Generic{rfl::Generic::Array{
                       rfl::Generic{static_cast<std::int64_t>(6)},
                       rfl::Generic{static_cast<std::int64_t>(7)},
                   }},
    };
    JSValue req_val = rpcpp::qjs::write(ctx, req);

    if (auto out = server.processMessage(req_val)) {
        JSValue resp = out->materialize(ctx);
        if (auto decoded = rpcpp::qjs::read<rpcpp::RpcResponse<int>>(ctx, resp)) {
            std::printf("6 * 7 = %d\n", decoded.value().result);
        }
        JS_FreeValue(ctx, resp);
    }

    JS_FreeValue(ctx, req_val);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    return 0;
}
