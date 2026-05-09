#pragma once

#include <functional>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>

#include <blockingconcurrentqueue.h>

#include <rfl.hpp>
#include <rfl/Generic.hpp>

#include "Codec.h"
#include "Framer.h"
#include "RpcEnvelope.h"
#include "Stdio.h"

namespace rpcpp {

template <class C,
          class InF  = typename C::default_in_framer,
          class OutF = typename C::default_out_framer>
    requires Codec<C>
class RpcServer {
public:
    using Handler = std::function<typename C::output_t(rfl::Generic id, rfl::Generic params)>;

    void addHandler(const std::string& name, Handler handler) {
        _handlers[name] = std::move(handler);
    }

    typename C::output_t processMessage(typename C::input_t bytes) {
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

        auto it = _handlers.find(req.method);
        if (it == _handlers.end()) {
            return C::write(RpcError{
                .id    = std::move(id),
                .error = { -32601, "Method not found: " + req.method },
            });
        }

        rfl::Generic params = req.params.has_value() ? *req.params : rfl::Generic{};

        try {
            return it->second(rfl::Generic{id}, std::move(params));
        } catch (const std::exception& e) {
            return C::write(RpcError{
                .id    = std::move(id),
                .error = { -32603, e.what() },
            });
        }
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
            _pending.enqueue(processMessage(view));
        }

        _pending.enqueue(std::nullopt);
    }

protected:
    std::unordered_map<std::string, Handler> _handlers;
    moodycamel::BlockingConcurrentQueue<std::optional<typename C::output_t>> _pending;
};

}  // namespace rpcpp
