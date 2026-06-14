#pragma once

#include <type_traits>

#include <quickjs.h>

#include <rfl/Processors.hpp>
#include <rfl/parsing/Parent.hpp>

#include "Parser.hpp"
#include "Writer.hpp"

namespace rpcpp::qjs {

// Encodes a C++ value into a freshly built QuickJS value. The returned JSValue
// is OWNED by the caller (free it with JS_FreeValue after use). Must be called
// on the thread that owns `_ctx`.
template <class... Ps>
JSValue write(JSContext* _ctx, const auto& _obj) {
  using T = std::remove_cvref_t<decltype(_obj)>;
  using ParentType = rfl::parsing::Parent<Writer>;
  Writer w(_ctx);
  Parser<T, rfl::Processors<Ps...>>::write(w, _obj, typename ParentType::Root{});
  return w.root();
}

}  // namespace rpcpp::qjs
