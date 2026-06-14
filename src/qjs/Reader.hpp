#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <quickjs.h>

#include <rfl/Result.hpp>
#include <rfl/always_false.hpp>

namespace rpcpp::qjs {

// reflect-cpp Reader that decodes C++ values directly from live QuickJS
// JSValues, mirroring rfl::json::Reader but over the QuickJS C API instead of
// yyjson. It plugs into rfl::parsing::Parser unchanged (see Parser.hpp).
//
// Ownership model (this is the correctness-critical part):
//   * The root JSValue handed to read() is owned by the CALLER and is never
//     freed here — only borrowed.
//   * Every value the reader itself fetches via JS_Get* is a +1 reference. We
//     never free those inline (the parser keeps using the handle); instead each
//     is registered in `owned_` and released together in the destructor. The
//     Reader lives for exactly one read() call, so this bounds the lifetime to
//     the decode and keeps the logic leak-free without inline free dances.
//   * JS_NULL / JS_UNDEFINED are not heap references, so tracking them is a
//     harmless no-op.
class Reader {
 public:
  struct InputArrayType {
    JSValue val_;
  };
  struct InputObjectType {
    JSValue val_;
  };
  struct InputVarType {
    JSValue val_;
  };

  template <class T>
  static constexpr bool has_custom_constructor = false;

  explicit Reader(JSContext* _ctx) : ctx_(_ctx) {}

  Reader(const Reader&) = delete;
  Reader& operator=(const Reader&) = delete;
  Reader(Reader&&) = delete;
  Reader& operator=(Reader&&) = delete;

  ~Reader() {
    for (JSValue v : owned_) {
      JS_FreeValue(ctx_, v);
    }
  }

  rfl::Result<InputVarType> get_field_from_array(
      const size_t _idx, const InputArrayType _arr) const noexcept {
    JSValue v = JS_GetPropertyUint32(ctx_, _arr.val_, static_cast<uint32_t>(_idx));
    if (JS_IsException(v) || JS_IsUndefined(v)) {
      JS_FreeValue(ctx_, v);
      return rfl::error("Index " + std::to_string(_idx) + " out of bounds.");
    }
    return track(v);
  }

  rfl::Result<InputVarType> get_field_from_object(
      const std::string& _name, const InputObjectType _obj) const noexcept {
    JSValue v = JS_GetPropertyStr(ctx_, _obj.val_, _name.c_str());
    if (JS_IsException(v) || JS_IsUndefined(v)) {
      JS_FreeValue(ctx_, v);
      return rfl::error("Object contains no field named '" + _name + "'.");
    }
    return track(v);
  }

  bool is_empty(const InputVarType _var) const noexcept {
    return JS_IsNull(_var.val_) || JS_IsUndefined(_var.val_);
  }

  template <class ArrayReader>
  std::optional<rfl::Error> read_array(const ArrayReader& _array_reader,
                                       const InputArrayType _arr) const noexcept {
    int64_t len = 0;
    if (JS_GetLength(ctx_, _arr.val_, &len) < 0) {
      return rfl::Error("Could not determine array length.");
    }
    for (int64_t i = 0; i < len; ++i) {
      JSValue v = JS_GetPropertyUint32(ctx_, _arr.val_, static_cast<uint32_t>(i));
      if (JS_IsException(v)) {
        JS_FreeValue(ctx_, v);
        return rfl::Error("Could not read array element " + std::to_string(i) + ".");
      }
      const auto err = _array_reader.read(track(v));
      if (err) {
        return err;
      }
    }
    return std::nullopt;
  }

  template <class ObjectReader>
  std::optional<rfl::Error> read_object(const ObjectReader& _object_reader,
                                        const InputObjectType _obj) const noexcept {
    JSPropertyEnum* tab = nullptr;
    uint32_t plen = 0;
    if (JS_GetOwnPropertyNames(ctx_, &tab, &plen, _obj.val_,
                               JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) < 0) {
      return rfl::Error("Could not enumerate object properties.");
    }
    for (uint32_t i = 0; i < plen; ++i) {
      const char* cname = JS_AtomToCString(ctx_, tab[i].atom);
      JSValue v = JS_GetProperty(ctx_, _obj.val_, tab[i].atom);
      if (cname && !JS_IsException(v)) {
        _object_reader.read(std::string_view(cname), track(v));
      } else {
        JS_FreeValue(ctx_, v);
      }
      if (cname) {
        JS_FreeCString(ctx_, cname);
      }
    }
    JS_FreePropertyEnum(ctx_, tab, plen);
    return std::nullopt;
  }

  template <class T>
  rfl::Result<T> to_basic_type(const InputVarType _var) const noexcept {
    using U = std::remove_cvref_t<T>;
    if constexpr (std::is_same_v<U, std::string>) {
      if (!JS_IsString(_var.val_)) {
        return rfl::error("Could not cast to string.");
      }
      size_t len = 0;
      const char* s = JS_ToCStringLen(ctx_, &len, _var.val_);
      if (!s) {
        return rfl::error("Could not cast to string.");
      }
      std::string out(s, len);
      JS_FreeCString(ctx_, s);
      return out;

    } else if constexpr (std::is_same_v<U, bool>) {
      if (!JS_IsBool(_var.val_)) {
        return rfl::error("Could not cast to boolean.");
      }
      return static_cast<bool>(JS_ToBool(ctx_, _var.val_));

    } else if constexpr (std::is_floating_point_v<U>) {
      if (!JS_IsNumber(_var.val_)) {
        return rfl::error("Could not cast to double.");
      }
      double d = 0.0;
      JS_ToFloat64(ctx_, &d, _var.val_);
      return static_cast<T>(d);

    } else if constexpr (std::is_integral_v<U>) {
      if (!JS_IsNumber(_var.val_)) {
        return rfl::error("Could not cast to integer.");
      }
      int64_t n = 0;
      JS_ToInt64(ctx_, &n, _var.val_);
      return static_cast<T>(n);

    } else {
      static_assert(rfl::always_false_v<T>, "Unsupported type.");
    }
  }

  rfl::Result<InputArrayType> to_array(const InputVarType _var) const noexcept {
    if (!JS_IsArray(_var.val_)) {
      return rfl::error("Could not cast to array!");
    }
    return InputArrayType{_var.val_};
  }

  rfl::Result<InputObjectType> to_object(
      const InputVarType _var) const noexcept {
    if (!JS_IsObject(_var.val_) || JS_IsArray(_var.val_)) {
      return rfl::error("Could not cast to object!");
    }
    return InputObjectType{_var.val_};
  }

  template <class T>
  rfl::Result<T> use_custom_constructor(
      const InputVarType) const noexcept {
    return rfl::error("Custom constructors are not supported by the QuickJS reader.");
  }

 private:
  InputVarType track(JSValue _v) const noexcept {
    owned_.push_back(_v);
    return InputVarType{_v};
  }

  JSContext* ctx_;
  mutable std::vector<JSValue> owned_;
};

}  // namespace rpcpp::qjs
