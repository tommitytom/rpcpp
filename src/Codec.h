#pragma once

#include <concepts>
#include <type_traits>

namespace rpcpp {

// A Codec is a thin policy over reflect-cpp's per-format read/write.
// Minimal contract: associated input/output/buffer types, default framers,
// and read<T>(input) / write(value) -> output.
template <class C>
concept Codec = requires {
    typename C::input_t;
    typename C::output_t;
    typename C::buffer_t;
    typename C::default_in_framer;
    typename C::default_out_framer;
};

// Optional opt-in for codecs that can decode a sub-value from a
// native AST node, skipping the rfl::Generic intermediate on the
// params path. Reserved for future optimization; not yet consumed
// by RpcServer.
template <class C>
concept NativeAstCodec = Codec<C> && requires { typename C::ast_view_t; };

}  // namespace rpcpp
