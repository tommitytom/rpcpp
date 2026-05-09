#pragma once

#include <optional>
#include <utility>

#include <concurrentqueue.h>

namespace rpcpp {

// In-memory transport backed by a lock-free MPMC queue. Producers can
// safely call send() from any thread; the owning side polls
// tryReceive() to drain responses on its own schedule.
//
// Use this when the RPC server lives in-process and the host wants to
// drive it from its own message loop instead of stdio. Sync handlers
// still return their response inline through processMessage(); only
// async dispatch results and explicit writeResponse / writeNotification
// / writeError calls land in the queue.
template <class C>
class QueueTransport {
public:
    using output_t = typename C::output_t;

    void send(output_t bytes) {
        _q.enqueue(std::move(bytes));
    }

    // Non-blocking dequeue. Returns nullopt when the queue is empty.
    std::optional<output_t> tryReceive() {
        output_t item;
        if (_q.try_dequeue(item)) return std::optional<output_t>{std::move(item)};
        return std::nullopt;
    }

private:
    moodycamel::ConcurrentQueue<output_t> _q;
};

}  // namespace rpcpp
