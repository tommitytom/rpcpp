#pragma once

#include <functional>

#include <quickjs.h>

#include "../qjs/read.hpp"
#include "../qjs/write.hpp"

namespace rpcpp {

// Output of the QuickJS codec. Construction of the actual JSValue is DEFERRED
// into this thunk so that QuickJSCodec::write(...) is safe to call from any
// thread — async handlers resolve on worker threads, but a JSContext is
// single-threaded. The thunk is invoked on the JS thread: by
// QuickJSTransport::drain for async responses/notifications, or directly by the
// caller for the synchronous processMessage return.
//
// The thunk captures only plain C++ values, never a JSValue, so moving a
// QuickJSOut across threads touches no QuickJS state.
struct QuickJSOut {
    std::function<JSValue(JSContext*)> materialize;
};

// In-process codec that marshals C++ values directly to/from QuickJS values via
// reflect-cpp (see src/qjs/*), with no byte serialization and no framing. It
// satisfies Codec but deliberately NOT WireCodec — there is no sensible way to
// put live JSValues on a stream transport, so StdioTransport will reject it.
//
// read() is only ever called from RpcServer::processMessage, which must run on
// the JS thread, so it builds eagerly. write() defers (see QuickJSOut).
class QuickJSCodec {
public:
    using input_t  = JSValue;
    using output_t = QuickJSOut;

    explicit QuickJSCodec(JSContext* ctx) : _ctx(ctx) {}

    template <class T>
    auto read(JSValue value) const {
        return qjs::read<T>(_ctx, value);
    }

    template <class T>
    output_t write(const T& value) const {
        return QuickJSOut{
            [value](JSContext* ctx) { return qjs::write(ctx, value); },
        };
    }

private:
    JSContext* _ctx;
};

}  // namespace rpcpp
