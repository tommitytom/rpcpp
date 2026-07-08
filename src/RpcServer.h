#pragma once

#include <atomic>
#include <concepts>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include <rfl.hpp>
#include <rfl/Generic.hpp>

#include "Codec.h"
#include "Framer.h"
#include "Resolver.h"
#include "RpcEnvelope.h"
#include "Transport.h"

namespace rpcpp {

template <class C>
    requires Codec<C>
class RpcServer {
public:
    // How params reach a handler: a NativeAstCodec passes the raw request AST (input_t) so the typed
    // layer can decode params straight into their tuple (Bytestring-capable); other codecs pass the
    // decoded rfl::Generic params, exactly as before.
    using ParamsArg = std::conditional_t<NativeAstCodec<C>, typename C::input_t, rfl::Generic>;

    using Handler      = std::function<typename C::output_t(rfl::Generic id, ParamsArg params)>;
    using AsyncHandler = std::function<void(rfl::Generic id, ParamsArg params, AsyncContext<C> ctx)>;

    // Bind the server to a transport. The transport reference must
    // outlive the server. The server is non-copyable / non-movable
    // because the transport binding is held as a closure capturing &.
    //
    // The codec is held by value: stateless byte codecs default-construct,
    // while a codec carrying host state (e.g. QuickJSCodec wrapping a
    // JSContext*) is passed in. All read/write goes through this instance.
    template <class T>
        requires Transport<T> && std::same_as<typename T::output_t, typename C::output_t>
    explicit RpcServer(T& transport, C codec = C{})
        : _codec(std::move(codec)),
          _send([&transport](typename C::output_t bytes) { transport.send(std::move(bytes)); }) {}

    RpcServer(const RpcServer&) = delete;
    RpcServer& operator=(const RpcServer&) = delete;
    RpcServer(RpcServer&&) = delete;
    RpcServer& operator=(RpcServer&&) = delete;

    void addHandler(const std::string& name, Handler handler) {
        _handlers[name] = std::move(handler);
    }

    void addAsyncHandler(const std::string& name, AsyncHandler handler) {
        _asyncHandlers[name] = std::move(handler);
    }

    std::optional<typename C::output_t> processMessage(typename C::input_t bytes) {
        if constexpr (NativeAstCodec<C>) {
            // Decode ONLY the envelope (method/id) — params stays an un-walked AST node the typed
            // handler decodes straight into its tuple, so a Uint8Array param never touches rfl::Generic.
            auto head_r = _codec.template read<RpcRequestHead>(bytes);
            if (!head_r) {
                return _codec.write(RpcError{
                    .id    = rfl::Generic{},
                    .error = { -32700, std::string{head_r.error().what()} },
                });
            }
            auto& head = head_r.value();
            rfl::Generic id = head.id.has_value() ? *head.id : rfl::Generic{};
            return route(head.method, std::move(id), head.jsonrpc, bytes);  // raw request as ParamsArg
        } else {
            auto req_r = _codec.template read<RpcRequest>(bytes);
            if (!req_r) {
                return _codec.write(RpcError{
                    .id    = rfl::Generic{},
                    .error = { -32700, std::string{req_r.error().what()} },
                });
            }
            auto& req = req_r.value();
            rfl::Generic id = req.id.has_value() ? *req.id : rfl::Generic{};
            rfl::Generic params = req.params.has_value() ? *req.params : rfl::Generic{};
            return route(req.method, std::move(id), req.jsonrpc, std::move(params));
        }
    }

    void writeResponse(typename C::output_t bytes) {
        _send(std::move(bytes));
    }

    void writeNotification(std::string method, rfl::Generic params = rfl::Generic{}) {
        _send(_codec.write(RpcNotification{
            .method = std::move(method),
            .params = std::move(params),
        }));
    }

    // Typed overload — preferred for push channels that carry binary or
    // otherwise non-trivial payloads. Serializes `params` directly instead of
    // routing it through `rfl::Generic`, which preserves `rfl::Bytestring`
    // as msgpack BIN (the Generic path would degrade it to an int array).
    //
    // Overload resolution: when the caller passes an `rfl::Generic` the
    // non-template form above is picked; any other type lands here.
    template <class T>
    void writeNotification(std::string method, const T& params) {
        _send(_codec.write(RpcTypedNotification<T>{
            .method = std::move(method),
            .params = params,
        }));
    }

    void writeError(rfl::Generic id, int code, std::string message) {
        _send(_codec.write(RpcError{
            .id    = std::move(id),
            .error = { code, std::move(message) },
        }));
    }

    // Block until every async handler in flight has resolved. Stdio-style
    // runners call this after their input stream closes so unfinished
    // resolves don't get stranded behind a shutdown sentinel.
    void waitForInflight() {
        std::unique_lock lk(_inflight_mtx);
        _inflight_cv.wait(lk, [this] { return _inflight.load() == 0; });
    }

    bool hasInflight() const noexcept {
        return _inflight.load() != 0;
    }

protected:
    // Shared dispatch tail for both processMessage branches: version check, handler lookup, sync
    // try/catch (-32603), async dispatch, method-not-found (-32601). `params` is the ParamsArg the
    // branch produced (the raw request AST for a NativeAstCodec, else the decoded rfl::Generic).
    std::optional<typename C::output_t> route(const std::string& method,
                                              rfl::Generic id,
                                              const std::string& jsonrpc,
                                              ParamsArg params) {
        if (jsonrpc != "2.0") {
            return _codec.write(RpcError{
                .id    = std::move(id),
                .error = { -32600, "Invalid JSON-RPC version" },
            });
        }

        if (auto sync_it = _handlers.find(method); sync_it != _handlers.end()) {
            try {
                return sync_it->second(rfl::Generic{id}, std::move(params));
            } catch (const std::exception& e) {
                return _codec.write(RpcError{
                    .id    = std::move(id),
                    .error = { -32603, e.what() },
                });
            }
        }

        if (auto async_it = _asyncHandlers.find(method); async_it != _asyncHandlers.end()) {
            return dispatchAsync(std::move(id), std::move(params), async_it->second);
        }

        return _codec.write(RpcError{
            .id    = std::move(id),
            .error = { -32601, "Method not found: " + method },
        });
    }

    std::optional<typename C::output_t> dispatchAsync(rfl::Generic id,
                                                      ParamsArg params,
                                                      AsyncHandler& handler) {
        auto state = std::make_shared<typename AsyncContext<C>::State>();
        state->onBytes = [this](typename C::output_t bytes) {
            _send(std::move(bytes));
        };
        state->onError = [this, id_copy = id](int code, std::string msg) mutable {
            _send(_codec.write(RpcError{
                .id    = std::move(id_copy),
                .error = { code, std::move(msg) },
            }));
        };
        state->onComplete = [this]() {
            if (_inflight.fetch_sub(1) == 1) {
                std::lock_guard lk(_inflight_mtx);
                _inflight_cv.notify_all();
            }
        };

        _inflight.fetch_add(1);

        try {
            handler(std::move(id), std::move(params), AsyncContext<C>{state});
        } catch (const std::exception& e) {
            if (!state->done.exchange(true) && state->onError) {
                state->onError(-32603, e.what());
            }
        }

        return std::nullopt;
    }

    C                                             _codec;
    std::function<void(typename C::output_t)>     _send;
    std::unordered_map<std::string, Handler>      _handlers;
    std::unordered_map<std::string, AsyncHandler> _asyncHandlers;

    std::atomic<int>        _inflight{0};
    std::mutex              _inflight_mtx;
    std::condition_variable _inflight_cv;
};

}  // namespace rpcpp
