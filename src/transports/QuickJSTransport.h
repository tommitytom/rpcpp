#pragma once

#include <functional>
#include <utility>

#include <quickjs.h>

#include <concurrentqueue.h>

#include "../codecs/QuickJSCodec.h"

namespace rpcpp {

// Transport for the in-process QuickJS codec. Async responses, notifications
// and errors are produced as deferred QuickJSOut thunks (see QuickJSCodec) and
// queued here from any thread; the owning JS thread materializes them into
// JSValues at drain() time and routes each to the host-supplied callback.
//
// Threading contract:
//   * send()  — safe to call from any thread (lock-free queue). Async handlers
//               resolving on worker threads land here.
//   * drain() — JS-THREAD ONLY. It is the single place a queued JSValue is
//               built and freed; call it from the JS event loop (e.g. per tick).
//
// The synchronous processMessage() return does NOT pass through this queue: the
// caller is already on the JS thread and materializes that QuickJSOut directly.
class QuickJSTransport {
public:
    using output_t = QuickJSOut;

    QuickJSTransport(JSContext* ctx,
                     std::function<void(JSContext*, JSValue)> onResponse)
        : _ctx(ctx), _onResponse(std::move(onResponse)) {}

    void send(output_t out) {
        _q.enqueue(std::move(out));
    }

    // Materialize and deliver every queued response. The JSValue handed to the
    // callback is owned by drain(): it is freed once the callback returns, so a
    // callback that needs to retain it must JS_DupValue.
    void drain() {
        output_t out;
        while (_q.try_dequeue(out)) {
            if (!out.materialize) continue;
            JSValue v = out.materialize(_ctx);
            _onResponse(_ctx, v);
            JS_FreeValue(_ctx, v);
        }
    }

private:
    JSContext* _ctx;
    std::function<void(JSContext*, JSValue)> _onResponse;
    moodycamel::ConcurrentQueue<output_t> _q;
};

}  // namespace rpcpp
