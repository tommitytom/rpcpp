#pragma once

#include <utility>

namespace rpcpp {

// A Transport is the destination for output bytes produced by the RPC
// server: encoded async responses, notifications, errors, and explicit
// writeResponse calls. (The synchronous return value of processMessage
// follows a different path and goes back to its caller directly.)
//
// The minimal contract is just send(): take ownership of an encoded
// payload and route it to wherever the peer is. Implementations decide
// how the peer reads it back — stdio, in-memory queue, socket, etc.
//
// send() must be safe to call from multiple threads; async handlers
// commonly resolve from worker threads.
template <class T>
concept Transport = requires(T t, typename T::output_t bytes) {
    typename T::output_t;
    { t.send(std::move(bytes)) };
};

}  // namespace rpcpp
