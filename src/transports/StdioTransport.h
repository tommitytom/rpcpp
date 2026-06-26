#pragma once

#include <iostream>
#include <optional>
#include <thread>
#include <utility>

#include <blockingconcurrentqueue.h>

#include "../Codec.h"
#include "../BinaryStdio.h"

namespace rpcpp {

// Stdio transport: pump bytes between an RpcServer and a pair of
// streams using the codec's framing. The dispatch loop that used to
// live in RpcServer::run() lives here, so the server itself doesn't
// need to know about streams.
//
// Pattern:
//
//     StdioTransport<JsonCodec> transport;
//     TypedRpcServer<MyService, JsonCodec> server(svc, transport);
//     server.addMethod<&MyService::foo>();
//     transport.run(server);                       // blocks on stdin EOF
//
// Output framing is serialized through a private blocking queue and a
// single responder thread, so async handlers resolving on worker
// threads can safely call send() concurrently.
template <class C,
          class InF  = typename C::default_in_framer,
          class OutF = typename C::default_out_framer>
    requires WireCodec<C>
class StdioTransport {
public:
    using output_t = typename C::output_t;

    void send(output_t bytes) {
        _pending.enqueue(std::move(bytes));
    }

    template <class Server>
    void run(Server& server,
             std::istream& in = std::cin,
             std::ostream& out = std::cout) {
        if constexpr (requires { C::is_binary; }) {
            if constexpr (C::is_binary) set_binary_stdio();
        }

        std::jthread responder([this, &out]() {
            std::optional<output_t> item;
            while (true) {
                _pending.wait_dequeue(item);
                if (!item.has_value()) return;
                OutF::write(out, *item);
                item.reset();
            }
        });

        while (auto frame = InF::template read<typename C::buffer_t>(in)) {
            typename C::input_t view{frame->data(), frame->size()};
            if (auto resp = server.processMessage(view)) {
                _pending.enqueue(std::move(*resp));
            }
        }

        // Wait for any async handlers still in flight before signalling
        // the responder to exit; otherwise their resolves race with the
        // exit sentinel and get stranded behind it.
        server.waitForInflight();

        _pending.enqueue(std::nullopt);
    }

private:
    moodycamel::BlockingConcurrentQueue<std::optional<output_t>> _pending;
};

}  // namespace rpcpp
