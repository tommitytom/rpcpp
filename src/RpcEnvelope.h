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

// Params-less routing view of a request. A NativeAstCodec decodes THIS to route on method/id
// without materializing params through rfl::Generic — the extra `params` field is ignored by the
// reader (a bare handle fetch, never a byte-walk), so a Uint8Array param survives untouched to be
// decoded typed by the handler.
struct RpcRequestHead {
    std::string method;
    std::optional<rfl::Generic> id;
    std::string jsonrpc = "2.0";
};

// Typed counterpart of RpcRequest — the inbound twin of RpcTypedNotification. A NativeAstCodec
// re-reads the raw request into this with `Params` == the method's decayed param tuple, so a
// binary param decodes as rfl::Bytestring (a Uint8Array) instead of degrading through rfl::Generic.
template <class Params>
struct RpcTypedRequest {
    std::string method;
    std::optional<rfl::Generic> id;
    std::optional<Params> params;
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
