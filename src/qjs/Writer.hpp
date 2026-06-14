#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

#include <quickjs.h>

#include <rfl/Bytestring.hpp>
#include <rfl/always_false.hpp>

namespace rpcpp::qjs {

// reflect-cpp Writer that builds a live QuickJS value tree, mirroring
// rfl::json::Writer but emitting JSValues via the QuickJS C API instead of a
// yyjson document. Plugs into rfl::parsing::Parser unchanged.
//
// Ownership model:
//   * Every JS_New* result is a +1 reference. The instant a child is attached
//     to its parent via JS_SetProperty*, that call STEALS the reference, so the
//     parent owns it and we must not free it again. The returned handle is a
//     borrow kept only so the parser can keep populating the child.
//   * The only value still owned when writing completes is the root, which
//     write() hands back to the caller (who frees it after use). There is no
//     document object to manage.
//   * QuickJS arrays have no append primitive, so OutputArrayType tracks the
//     next index to assign.
class Writer {
 public:
  struct OutputArrayType {
    JSValue val_;
    uint32_t length_ = 0;
  };
  struct OutputObjectType {
    JSValue val_;
  };
  struct OutputVarType {
    JSValue val_;
    OutputVarType(JSValue _v) : val_(_v) {}
    OutputVarType(OutputArrayType _arr) : val_(_arr.val_) {}
    OutputVarType(OutputObjectType _obj) : val_(_obj.val_) {}
  };

  explicit Writer(JSContext* _ctx) : ctx_(_ctx) {}

  // The root-producing methods record the root as a side effect, because
  // rfl::parsing::Parser::write returns void; write() reads it back via root().
  OutputArrayType array_as_root(const size_t) const noexcept {
    JSValue v = JS_NewArray(ctx_);
    root_ = v;
    return OutputArrayType{v};
  }

  OutputObjectType object_as_root(const size_t) const noexcept {
    JSValue v = JS_NewObject(ctx_);
    root_ = v;
    return OutputObjectType{v};
  }

  OutputVarType null_as_root() const noexcept {
    root_ = JS_NULL;
    return OutputVarType{JS_NULL};
  }

  template <class T>
  OutputVarType value_as_root(const T& _var) const noexcept {
    JSValue v = from_basic_type(_var);
    root_ = v;
    return OutputVarType{v};
  }

  // The owned root JSValue, valid after Parser::write completes.
  JSValue root() const noexcept { return root_; }

  OutputArrayType add_array_to_array(const size_t,
                                     OutputArrayType* _parent) const {
    JSValue child = JS_NewArray(ctx_);
    set_in_array(_parent, child);
    return OutputArrayType{child};
  }

  OutputArrayType add_array_to_object(const std::string_view& _name,
                                      const size_t,
                                      OutputObjectType* _parent) const {
    JSValue child = JS_NewArray(ctx_);
    set_in_object(_parent, _name, child);
    return OutputArrayType{child};
  }

  OutputObjectType add_object_to_array(const size_t,
                                       OutputArrayType* _parent) const {
    JSValue child = JS_NewObject(ctx_);
    set_in_array(_parent, child);
    return OutputObjectType{child};
  }

  OutputObjectType add_object_to_object(const std::string_view& _name,
                                        const size_t,
                                        OutputObjectType* _parent) const {
    JSValue child = JS_NewObject(ctx_);
    set_in_object(_parent, _name, child);
    return OutputObjectType{child};
  }

  template <class T>
  OutputVarType add_value_to_array(const T& _var,
                                   OutputArrayType* _parent) const {
    JSValue child = from_basic_type(_var);
    set_in_array(_parent, child);
    return OutputVarType{child};
  }

  template <class T>
  OutputVarType add_value_to_object(const std::string_view& _name,
                                    const T& _var,
                                    OutputObjectType* _parent) const {
    JSValue child = from_basic_type(_var);
    set_in_object(_parent, _name, child);
    return OutputVarType{child};
  }

  OutputVarType add_null_to_array(OutputArrayType* _parent) const {
    set_in_array(_parent, JS_NULL);
    return OutputVarType{JS_NULL};
  }

  OutputVarType add_null_to_object(const std::string_view& _name,
                                   OutputObjectType* _parent) const {
    set_in_object(_parent, _name, JS_NULL);
    return OutputVarType{JS_NULL};
  }

  void end_array(OutputArrayType*) const noexcept {}
  void end_object(OutputObjectType*) const noexcept {}

 private:
  void set_in_array(OutputArrayType* _parent, JSValue _child) const {
    // JS_SetPropertyUint32 steals the reference to _child.
    if (JS_SetPropertyUint32(ctx_, _parent->val_, _parent->length_, _child) < 0) {
      throw std::runtime_error("Could not append value to QuickJS array.");
    }
    ++_parent->length_;
  }

  void set_in_object(OutputObjectType* _parent, const std::string_view& _name,
                     JSValue _child) const {
    // JS_SetPropertyStr steals the reference to _child; the name must be
    // null-terminated, so materialize the view into a std::string.
    const std::string name(_name);
    if (JS_SetPropertyStr(ctx_, _parent->val_, name.c_str(), _child) < 0) {
      throw std::runtime_error("Could not set field '" + name +
                               "' on QuickJS object.");
    }
  }

  template <class T>
  JSValue from_basic_type(const T& _var) const noexcept {
    using U = std::remove_cvref_t<T>;
    if constexpr (std::is_same_v<U, std::string>) {
      return JS_NewStringLen(ctx_, _var.data(), _var.size());
    } else if constexpr (std::is_same_v<U, rfl::Bytestring>) {
      // Binary buffers (rfl::Bytestring) cross the bridge as a JS Uint8Array,
      // matching the msgpack codec's BIN path — not a JS array of numbers.
      return JS_NewUint8ArrayCopy(
          ctx_, reinterpret_cast<const uint8_t*>(_var.data()), _var.size());
    } else if constexpr (std::is_same_v<U, bool>) {
      return JS_NewBool(ctx_, _var);
    } else if constexpr (std::is_floating_point_v<U>) {
      return JS_NewFloat64(ctx_, static_cast<double>(_var));
    } else if constexpr (std::is_integral_v<U>) {
      return JS_NewInt64(ctx_, static_cast<int64_t>(_var));
    } else {
      static_assert(rfl::always_false_v<T>, "Unsupported type.");
    }
  }

  JSContext* ctx_;
  mutable JSValue root_ = JS_UNDEFINED;
};

}  // namespace rpcpp::qjs
