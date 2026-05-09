#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <utility>

namespace rpcpp {

// AsyncContext<C> is the codec-aware low-level handle handed to raw async
// handlers registered via RpcServer::addAsyncHandler. It carries the
// machinery to push an already-encoded response (or an error envelope)
// onto the server's pending queue from any thread.
//
// AsyncContext is copyable; the underlying State has a once-flag so the
// first respondBytes / respondError wins and later calls no-op. If every
// copy of an AsyncContext is destroyed without firing, the destructor
// auto-emits -32603 "Async request was abandoned".
template <class C>
class AsyncContext {
public:
    AsyncContext(const AsyncContext&) = default;
    AsyncContext& operator=(const AsyncContext&) = default;
    AsyncContext(AsyncContext&&) noexcept = default;
    AsyncContext& operator=(AsyncContext&&) noexcept = default;
    ~AsyncContext() = default;

    void respondBytes(typename C::output_t bytes) {
        if (auto& s = *_state; !s.done.exchange(true)) {
            if (s.onBytes) s.onBytes(std::move(bytes));
        }
    }

    void respondError(int code, std::string message) {
        if (auto& s = *_state; !s.done.exchange(true)) {
            if (s.onError) s.onError(code, std::move(message));
        }
    }

    bool resolved() const noexcept { return _state->done.load(); }

    struct State {
        std::atomic<bool> done{false};
        std::function<void(typename C::output_t)> onBytes;
        std::function<void(int, std::string)>     onError;
        std::function<void()>                     onComplete;

        ~State() {
            if (!done.exchange(true) && onError) {
                onError(-32603, "Async request was abandoned");
            }
            if (onComplete) onComplete();
        }
    };

    explicit AsyncContext(std::shared_ptr<State> state) : _state(std::move(state)) {}

private:
    std::shared_ptr<State> _state;
};

// Resolver<R> is the typed handle handed to user methods registered via
// TypedRpcServer::addAsyncMethod. Method signature is
//
//     void method(Args..., rpcpp::Resolver<R> r)
//
// The user calls r.resolve(value) or r.reject(code, msg) from any thread.
// Like AsyncContext, the first call wins; abandoning every copy of the
// Resolver auto-emits -32603. Resolver is copyable so it can be cloned
// into multiple closures freely.
template <class R>
class Resolver {
public:
    Resolver(const Resolver&) = default;
    Resolver& operator=(const Resolver&) = default;
    Resolver(Resolver&&) noexcept = default;
    Resolver& operator=(Resolver&&) noexcept = default;
    ~Resolver() = default;

    void resolve(R value) {
        if (auto& s = *_state; !s.done.exchange(true)) {
            if (s.onResolve) s.onResolve(std::move(value));
        }
    }

    void reject(int code, std::string message) {
        if (auto& s = *_state; !s.done.exchange(true)) {
            if (s.onReject) s.onReject(code, std::move(message));
        }
    }

    bool resolved() const noexcept { return _state->done.load(); }

    struct State {
        std::atomic<bool> done{false};
        std::function<void(R)>                onResolve;
        std::function<void(int, std::string)> onReject;

        ~State() {
            if (!done.exchange(true) && onReject) {
                onReject(-32603, "Async request was abandoned");
            }
        }
    };

    explicit Resolver(std::shared_ptr<State> state) : _state(std::move(state)) {}

private:
    std::shared_ptr<State> _state;
};

}  // namespace rpcpp
