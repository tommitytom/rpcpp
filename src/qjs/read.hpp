#pragma once

#include <quickjs.h>

#include <rfl/Processors.hpp>
#include <rfl/Result.hpp>
#include <rfl/internal/wrap_in_rfl_array_t.hpp>

#include "Parser.hpp"
#include "Reader.hpp"

namespace rpcpp::qjs {

// Decodes a C++ value of type T directly from a live QuickJS value. The input
// `_val` is borrowed: ownership stays with the caller; the Reader only reads
// from it (and frees any intermediate references it fetches itself). Must be
// called on the thread that owns `_ctx` (QuickJS contexts are single-threaded).
template <class T, class... Ps>
rfl::Result<rfl::internal::wrap_in_rfl_array_t<T>> read(JSContext* _ctx,
                                                        JSValue _val) {
  Reader r(_ctx);
  return Parser<T, rfl::Processors<Ps...>>::read(r, Reader::InputVarType{_val});
}

}  // namespace rpcpp::qjs
