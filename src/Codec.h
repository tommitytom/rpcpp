#pragma once

#include <concepts>
#include <type_traits>

namespace rpcpp {

// A Codec is a thin policy over reflect-cpp's per-format read/write.
// Minimal contract: associated input/output types plus read<T>(input)
// and write(value) -> output. read/write may be static (the stateless
// byte codecs) or instance methods (a codec carrying host state, e.g.
// the QuickJS codec holding a JSContext*); the server always invokes
// them through a held codec instance, so both forms satisfy this.
template <class C>
concept Codec = requires {
    typename C::input_t;
    typename C::output_t;
};

// A WireCodec additionally serializes to/from a framed byte stream: it
// names a buffer type and the default framers a stream transport should
// use. The stdio transport requires this refinement; in-process codecs
// (QuickJS) deliberately do not provide it.
template <class C>
concept WireCodec = Codec<C> && requires {
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
