#pragma once

#include <exception>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include <rfl.hpp>
#include <rfl/Generic.hpp>
#include <rfl/from_generic.hpp>
#include <rfl/json.hpp>

#include "RpcEnvelope.h"
#include "RpcServer.h"

namespace rpcpp {

struct VoidT {};

namespace detail {

template <auto MemFn>
constexpr std::string_view methodName() {
#if defined(_MSC_VER) && !defined(__clang__)
    constexpr std::string_view sig = __FUNCSIG__;
    constexpr std::string_view startMarker = "methodName<";
    constexpr char endChar = '>';
#else
    constexpr std::string_view sig = __PRETTY_FUNCTION__;
    constexpr std::string_view startMarker = "MemFn = ";
    constexpr char endChar = ';';
#endif

    const auto startPos = sig.find(startMarker);
    if (startPos == std::string_view::npos) return {};
    const auto argStart = startPos + startMarker.size();

    auto argEnd = sig.size();
    for (auto i = argStart; i < sig.size(); ++i) {
        if (sig[i] == ';' || sig[i] == ']' || sig[i] == endChar) {
            argEnd = i;
            break;
        }
    }

    const std::string_view arg = sig.substr(argStart, argEnd - argStart);
    const auto lastColons = arg.rfind("::");
    const auto nameStart = (lastColons == std::string_view::npos) ? 0 : lastColons + 2;

    auto nameEnd = nameStart;
    while (nameEnd < arg.size()) {
        const char c = arg[nameEnd];
        const bool isIdent =
            (c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == '_';
        if (!isIdent) break;
        ++nameEnd;
    }
    return arg.substr(nameStart, nameEnd - nameStart);
}

template <typename, bool, bool>
struct FunctionMeta;

template <typename Ret, typename... Args, bool Const, bool Static>
struct FunctionMeta<Ret(Args...), Const, Static> {
    using ReturnType         = std::conditional_t<std::is_same_v<Ret, void>, VoidT, Ret>;
    using ParamTuple         = std::tuple<std::decay_t<Args>...>;
    using OriginalArgsTuple  = std::tuple<Args...>;
};

template <typename Type, typename Ret, typename... Args, typename Class>
constexpr FunctionMeta<std::conditional_t<std::is_same_v<Type, Class>, Ret(Args...), Ret(Class&, Args...)>,
                       true, !std::is_same_v<Type, Class>>
toFunctionMeta(Ret(Class::*)(Args...) const);

template <typename Type, typename Ret, typename... Args, typename Class>
constexpr FunctionMeta<std::conditional_t<std::is_same_v<Type, Class>, Ret(Args...), Ret(Class&, Args...)>,
                       false, !std::is_same_v<Type, Class>>
toFunctionMeta(Ret(Class::*)(Args...));

template <typename Type, typename Ret, typename... Args>
constexpr FunctionMeta<Ret(Args...), false, true> toFunctionMeta(Ret(*)(Args...));

template <typename Type>
constexpr void toFunctionMeta(...);

template <typename Type, typename Candidate>
using FunctionMetaT = decltype(toFunctionMeta<Type>(std::declval<Candidate>()));

template <typename OrigArg, typename StoredArg>
decltype(auto) forwardArg(StoredArg& arg) {
    if constexpr (std::is_lvalue_reference_v<OrigArg>) {
        return static_cast<StoredArg&>(arg);
    } else {
        return static_cast<StoredArg&&>(std::move(arg));
    }
}

template <auto Func, typename T, typename ParamTuple, typename OrigArgsTuple, std::size_t... Is>
decltype(auto) invokeMethod(T& obj, ParamTuple& args, std::index_sequence<Is...>) {
    return (obj.*Func)(forwardArg<std::tuple_element_t<Is, OrigArgsTuple>>(std::get<Is>(args))...);
}

inline void replaceAll(std::string& str, const std::string& from, const std::string& to) {
    size_t pos = 0;
    while ((pos = str.find(from, pos)) != std::string::npos) {
        str.replace(pos, from.length(), to);
        pos += to.length();
    }
}

inline std::string extractTypeName(const std::string& typeStr) {
    if (typeStr.starts_with("#")) {
        return typeStr.substr(typeStr.find_last_of("/") + 1);
    }
    return typeStr;
}

struct MethodDesc {
    std::vector<std::string> params;
    std::string ret;
};

using SchemaGenerator = std::function<std::string()>;

}  // namespace detail

template <class T, class C, class InF = typename C::default_in_framer, class OutF = typename C::default_out_framer>
    requires Codec<C>
class TypedRpcServer : public RpcServer<C, InF, OutF> {
public:
    explicit TypedRpcServer(T& object) : _object(object) {}

    std::string dumpSchema() {
        return rfl::json::write(generateSchema(), rfl::json::pretty);
    }

    rfl::Generic::Object generateSchema() {
        rfl::Generic::Object schemas;
        for (const auto& [name, generator] : _schemaGenerators) {
            std::string schemaStr = generator();
            detail::replaceAll(schemaStr, "#/$defs/", "#/components/schemas/");

            auto result = rfl::json::read<rfl::Generic::Object>(schemaStr);
            if (!result) {
                throw std::runtime_error(result.error().what());
            }

            // Bind the Result<Object> to a named local so the for-range
            // doesn't iterate dangling memory from a destroyed temporary.
            auto defs_r = result.value()["$defs"].to_object();
            if (!defs_r) {
                throw std::runtime_error(defs_r.error().what());
            }
            for (const auto& [defName, defSchema] : defs_r.value()) {
                if (defName == "VoidT") continue;
                auto inner = defSchema.to_object();
                if (!inner) throw std::runtime_error(inner.error().what());
                schemas[defName] = inner.value();
            }
        }

        rfl::Generic::Object components;
        components["schemas"] = schemas;

        rfl::Generic::Array methods;
        for (const auto& [methodName, methodDesc] : _methodSchemas) {
            std::size_t paramIdx = 0;
            rfl::Generic::Array params;
            for (const auto& typeName : methodDesc.params) {
                rfl::Generic::Object param;
                param["name"]     = "param" + std::to_string(paramIdx++);
                param["required"] = true;

                rfl::Generic::Object paramSchema;
                if (typeName.starts_with("#")) {
                    paramSchema["$ref"] = typeName.empty() ? "null" : typeName;
                } else {
                    paramSchema["type"] = typeName;
                }
                param["schema"] = paramSchema;
                params.push_back(param);
            }

            rfl::Generic::Object resultSchema;
            std::string returnType = methodDesc.ret.empty() ? "null" : methodDesc.ret;
            if (returnType.starts_with("#")) {
                resultSchema["$ref"] = returnType;
            } else {
                resultSchema["type"] = returnType;
            }

            rfl::Generic::Object result;
            result["name"]   = detail::extractTypeName(returnType);
            result["schema"] = resultSchema;

            rfl::Generic::Object method;
            method["name"]   = methodName;
            method["params"] = params;
            method["result"] = result;
            methods.push_back(method);
        }

        rfl::Generic::Object info;
        info["title"]   = "JSON-RPC API Schema";
        info["version"] = "1.0.0";

        rfl::Generic::Object root;
        root["openrpc"]    = "1.3.2";
        root["info"]       = info;
        root["methods"]    = methods;
        root["components"] = components;
        return root;
    }

    template <class U>
    std::string getJsonTypeName() {
        if constexpr (std::is_same_v<U, bool>) {
            return "boolean";
        } else if constexpr (std::is_integral_v<U>) {
            return "integer";
        } else if constexpr (std::is_floating_point_v<U>) {
            return "number";
        } else if constexpr (std::is_same_v<U, VoidT>) {
            return "null";
        } else if constexpr (std::is_same_v<U, std::string>) {
            return "string";
        } else if constexpr (std::is_class_v<U>) {
            return "#/components/schemas/" + rfl::parsing::make_type_name<U>();
        } else {
            static_assert(rfl::always_false_v<U>, "Unsupported type for JSON schema generation.");
        }
    }

    template <class U>
    void addSchemaGenerator() {
        const std::string name = rfl::parsing::make_type_name<U>();
        if (!_schemaGenerators.contains(name)) {
            _schemaGenerators[name] = []() { return rfl::json::to_schema<U>(); };
        }
    }

    template <class U>
    void addParam(const std::string& methodName) {
        if constexpr (std::is_class_v<U>) addSchemaGenerator<U>();
        _methodSchemas[methodName].params.push_back(getJsonTypeName<U>());
    }

    template <class U>
    void addReturn(const std::string& methodName) {
        if constexpr (std::is_class_v<U>) addSchemaGenerator<U>();
        _methodSchemas[methodName].ret = getJsonTypeName<U>();
    }

    void addDiscoveryMethod() {
        this->addHandler("rpc.discover",
            [this](rfl::Generic id, rfl::Generic /*params*/) -> typename C::output_t {
                return C::write(RpcResponse<rfl::Generic::Object>{
                    .id     = std::move(id),
                    .result = generateSchema(),
                });
            });
    }

    template <auto Func>
    TypedRpcServer& addMethod(const std::string& name) {
        using Meta       = detail::FunctionMetaT<T, decltype(Func)>;
        using ParamTuple = typename Meta::ParamTuple;
        using ReturnT    = typename Meta::ReturnType;
        using OrigArgs   = typename Meta::OriginalArgsTuple;
        constexpr auto N = std::tuple_size_v<ParamTuple>;

        std::apply([this, &name](auto&&... args) {
            ((addParam<std::remove_cvref_t<decltype(args)>>(name)), ...);
        }, ParamTuple{});

        addReturn<std::remove_cvref_t<ReturnT>>(name);

        this->addHandler(name,
            [this](rfl::Generic id, rfl::Generic params) -> typename C::output_t {
                auto args_r = rfl::from_generic<ParamTuple>(params);
                if (!args_r) {
                    throw std::runtime_error(args_r.error().what());
                }
                auto args = std::move(args_r).value();

                if constexpr (std::is_same_v<ReturnT, VoidT>) {
                    detail::invokeMethod<Func, T, ParamTuple, OrigArgs>(
                        _object, args, std::make_index_sequence<N>{});
                    return C::write(RpcResponse<rfl::Generic>{
                        .id     = std::move(id),
                        .result = rfl::Generic{},
                    });
                } else {
                    auto ret = detail::invokeMethod<Func, T, ParamTuple, OrigArgs>(
                        _object, args, std::make_index_sequence<N>{});
                    return C::write(RpcResponse<std::remove_cvref_t<ReturnT>>{
                        .id     = std::move(id),
                        .result = std::move(ret),
                    });
                }
            });
        return *this;
    }

    template <auto MemFn>
    TypedRpcServer& addMethod() {
        constexpr auto name = detail::methodName<MemFn>();
        static_assert(!name.empty(), "Failed to parse method name from function signature");
        return addMethod<MemFn>(std::string(name));
    }

private:
    T& _object;
    std::unordered_map<std::string, detail::SchemaGenerator> _schemaGenerators;
    std::map<std::string, detail::MethodDesc> _methodSchemas;
};

}  // namespace rpcpp
