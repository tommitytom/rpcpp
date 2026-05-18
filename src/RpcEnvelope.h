#pragma once

#include <optional>
#include <string>

#include <rfl.hpp>
#include <rfl/Generic.hpp>

namespace rpcpp {

struct RpcRequest {
    std::string method;
    std::optional<rfl::Generic> id;
    std::optional<rfl::Generic> params;
    std::string jsonrpc = "2.0";
};

template <class Result>
struct RpcResponse {
    rfl::Generic id;
    Result result;
    std::string jsonrpc = "2.0";
};

struct ErrorBody {
    int code;
    std::string message;
};

struct RpcError {
    rfl::Generic id;
    ErrorBody error;
    std::string jsonrpc = "2.0";
};

struct RpcNotification {
    std::string method;
    rfl::Generic params;
    std::string jsonrpc = "2.0";
};

// Typed counterpart of RpcNotification — used by RpcServer::writeNotification<T>
// to push a notification whose params are a reflect-cpp-serializable struct,
// without round-tripping through rfl::Generic. The Generic path works for
// plain values, but it silently degrades rfl::Bytestring to a vector-of-ints
// and explodes wire size; the typed path preserves msgpack BIN end-to-end.
template <class Params>
struct RpcTypedNotification {
    std::string method;
    Params params;
    std::string jsonrpc = "2.0";
};

}  // namespace rpcpp
