#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>

#include <blockingconcurrentqueue.h>

#include <rfl.hpp>
#include <rfl/Generic.hpp>

#include "Codec.h"
#include "Framer.h"
#include "Resolver.h"
#include "RpcEnvelope.h"
#include "Stdio.h"

namespace rpcpp {

template <class C,
          class InF  = typename C::default_in_framer,
          class OutF = typename C::default_out_framer>
    requires Codec<C>
class RpcServer {
public:
    using Handler      = std::function<typename C::output_t(rfl::Generic id, rfl::Generic params)>;
    using AsyncHandler = std::function<void(rfl::Generic id, rfl::Generic params, AsyncContext<C> ctx)>;

    void addHandler(const std::string& name, Handler handler) {
        _handlers[name] = std::move(handler);
    }

    void addAsyncHandler(const std::string& name, AsyncHandler handler) {
        _asyncHandlers[name] = std::move(handler);
    }

    std::optional<typename C::output_t> processMessage(typename C::input_t bytes) {
        auto req_r = C::template read<RpcRequest>(bytes);
        if (!req_r) {
            return C::write(RpcError{
                .id    = rfl::Generic{},
                .error = { -32700, std::string{req_r.error().what()} },
            });
        }

        const auto& req = req_r.value();
        rfl::Generic id = req.id.has_value() ? *req.id : rfl::Generic{};

        if (req.jsonrpc != "2.0") {
            return C::write(RpcError{
                .id    = std::move(id),
                .error = { -32600, "Invalid JSON-RPC version" },
            });
        }

        rfl::Generic params = req.params.has_value() ? *req.params : rfl::Generic{};

        if (auto sync_it = _handlers.find(req.method); sync_it != _handlers.end()) {
            try {
                return sync_it->second(rfl::Generic{id}, std::move(params));
            } catch (const std::exception& e) {
                return C::write(RpcError{
                    .id    = std::move(id),
                    .error = { -32603, e.what() },
                });
            }
        }

        if (auto async_it = _asyncHandlers.find(req.method); async_it != _asyncHandlers.end()) {
            return dispatchAsync(std::move(id), std::move(params), async_it->second);
        }

        return C::write(RpcError{
            .id    = std::move(id),
            .error = { -32601, "Method not found: " + req.method },
        });
    }

    void writeResponse(typename C::output_t bytes) {
        _pending.enqueue(std::move(bytes));
    }

    void writeNotification(std::string method, rfl::Generic params = rfl::Generic{}) {
        _pending.enqueue(C::write(RpcNotification{
            .method = std::move(method),
            .params = std::move(params),
        }));
    }

    void writeError(rfl::Generic id, int code, std::string message) {
        _pending.enqueue(C::write(RpcError{
            .id    = std::move(id),
            .error = { code, std::move(message) },
        }));
    }

    void run(std::istream& in = std::cin, std::ostream& out = std::cout) {
        if constexpr (requires { C::is_binary; }) {
            if constexpr (C::is_binary) set_binary_stdio();
        }

        std::jthread responder([this, &out]() {
            std::optional<typename C::output_t> item;
            while (true) {
                _pending.wait_dequeue(item);
                if (!item.has_value()) return;
                OutF::write(out, *item);
                item.reset();
            }
        });

        while (auto frame = InF::template read<typename C::buffer_t>(in)) {
            typename C::input_t view{frame->data(), frame->size()};
            if (auto resp = processMessage(view)) {
                _pending.enqueue(std::move(*resp));
            }
        }

        // Wait for any async handlers still in flight before signalling
        // the responder to exit; otherwise their resolves race with the
        // exit sentinel and get stranded behind it.
        {
            std::unique_lock lk(_inflight_mtx);
            _inflight_cv.wait(lk, [this] { return _inflight.load() == 0; });
        }

        _pending.enqueue(std::nullopt);
    }

protected:
    std::optional<typename C::output_t> dispatchAsync(rfl::Generic id,
                                                      rfl::Generic params,
                                                      AsyncHandler& handler) {
        auto state = std::make_shared<typename AsyncContext<C>::State>();
        state->onBytes = [this](typename C::output_t bytes) {
            _pending.enqueue(std::move(bytes));
        };
        state->onError = [this, id_copy = id](int code, std::string msg) mutable {
            _pending.enqueue(C::write(RpcError{
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

    std::unordered_map<std::string, Handler>      _handlers;
    std::unordered_map<std::string, AsyncHandler> _asyncHandlers;
    moodycamel::BlockingConcurrentQueue<std::optional<typename C::output_t>> _pending;

    std::atomic<int>        _inflight{0};
    std::mutex              _inflight_mtx;
    std::condition_variable _inflight_cv;
};

}  // namespace rpcpp
