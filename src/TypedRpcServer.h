#pragma once

#include <concepts>
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

constexpr bool isIdentChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

template <auto MemFn>
constexpr std::string_view methodName() {
#if defined(_MSC_VER) && !defined(__clang__)
    // MSVC renders the pointer-to-member non-type template argument as the
    // function's FULL signature (not `&Class::method`), e.g.
    //   methodName<unsigned int __cdecl Class::method(std::basic_string<...>,...)>(void)
    // The earlier `rfind("::")` approach mis-fires on the `std::`/`char_traits`
    // qualifiers inside templated parameter types. Anchor instead on the first
    // '(' after the marker — the method's parameter list — and walk back over
    // the identifier that ends there.
    constexpr std::string_view sig = __FUNCSIG__;
    constexpr std::string_view startMarker = "methodName<";

    const auto startPos = sig.find(startMarker);
    if (startPos == std::string_view::npos) return {};
    const auto argStart = startPos + startMarker.size();

    const auto parenPos = sig.find('(', argStart);
    if (parenPos == std::string_view::npos) return {};

    auto nameStart = parenPos;
    while (nameStart > argStart && isIdentChar(sig[nameStart - 1])) --nameStart;
    return sig.substr(nameStart, parenPos - nameStart);
#else
    constexpr std::string_view sig = __PRETTY_FUNCTION__;
    constexpr std::string_view startMarker = "MemFn = ";
    constexpr char endChar = ';';

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
    while (nameEnd < arg.size() && isIdentChar(arg[nameEnd])) ++nameEnd;
    return arg.substr(nameStart, nameEnd - nameStart);
#endif
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

// ── Async helpers ───────────────────────────────────────────────────────────

template <class X> struct IsResolver : std::false_type {};
template <class R> struct IsResolver<Resolver<R>> : std::true_type {};

template <class X> struct ResolverInner;
template <class R> struct ResolverInner<Resolver<R>> { using type = R; };

template <class Tup> struct DecayTuple;
template <class... Ts>
struct DecayTuple<std::tuple<Ts...>> {
    using type = std::tuple<std::decay_t<Ts>...>;
};

template <class... Args>
struct PeelLast {
private:
    template <std::size_t... Is>
    static auto rest_helper(std::index_sequence<Is...>)
        -> std::tuple<std::tuple_element_t<Is, std::tuple<Args...>>...>;

public:
    static constexpr std::size_t N = sizeof...(Args);
    static_assert(N >= 1, "Async method must have at least one parameter (Resolver<R>)");
    using rest = decltype(rest_helper(std::make_index_sequence<N - 1>{}));
    using last = std::tuple_element_t<N - 1, std::tuple<Args...>>;
};

template <class Class, class... Args>
constexpr auto asyncFnMeta(void(Class::*)(Args...)) {
    using P    = PeelLast<Args...>;
    using Last = std::remove_cvref_t<typename P::last>;
    static_assert(IsResolver<Last>::value,
                  "addAsyncMethod requires the method's last parameter to be rpcpp::Resolver<R>");
    using R   = typename ResolverInner<Last>::type;
    using PT  = typename DecayTuple<typename P::rest>::type;
    using OAT = typename P::rest;
    return std::tuple<PT, OAT, R>{};
}

template <class Class, class... Args>
constexpr auto asyncFnMeta(void(Class::*)(Args...) const) {
    using P    = PeelLast<Args...>;
    using Last = std::remove_cvref_t<typename P::last>;
    static_assert(IsResolver<Last>::value,
                  "addAsyncMethod requires the method's last parameter to be rpcpp::Resolver<R>");
    using R   = typename ResolverInner<Last>::type;
    using PT  = typename DecayTuple<typename P::rest>::type;
    using OAT = typename P::rest;
    return std::tuple<PT, OAT, R>{};
}

template <auto Func>
using AsyncFnMetaT = decltype(asyncFnMeta(Func));

template <auto Func, typename T, typename ParamTuple, typename OrigArgsTuple,
          typename ResolverT, std::size_t... Is>
void invokeAsync(T& obj, ParamTuple& args, ResolverT&& resolver,
                 std::index_sequence<Is...>) {
    (obj.*Func)(forwardArg<std::tuple_element_t<Is, OrigArgsTuple>>(std::get<Is>(args))...,
                std::forward<ResolverT>(resolver));
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

template <class T, class C>
    requires Codec<C>
class TypedRpcServer : public RpcServer<C> {
public:
    template <class Tr>
        requires Transport<Tr> && std::same_as<typename Tr::output_t, typename C::output_t>
    TypedRpcServer(T& object, Tr& transport, C codec = C{})
        : RpcServer<C>(transport, std::move(codec)), _object(object) {}

    std::string dumpSchema() {
        return rfl::json::write(generateSchema(), rfl::json::pretty);
    }

    rfl::Generic::Object generateSchema() {
        rfl::Generic::Object schemas;
        auto contains_key = [](const rfl::Generic::Object& o, std::string_view key) {
            for (const auto& [k, _] : o) {
                if (k == key) return true;
            }
            return false;
        };

        for (const auto& [name, generator] : _schemaGenerators) {
            std::string schemaStr = generator();
            detail::replaceAll(schemaStr, "#/$defs/", "#/components/schemas/");

            auto result = rfl::json::read<rfl::Generic::Object>(schemaStr);
            if (!result) {
                throw std::runtime_error(result.error().what());
            }

            // Bind the Result<Object> to a named local so the for-range
            // doesn't iterate dangling memory from a destroyed temporary.
            auto root = result.value();

            // 1. Export the inner $defs (the types the wrapper references).
            if (contains_key(root, "$defs")) {
                auto defs_r = root["$defs"].to_object();
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

            // 2. Also export the top-level schema itself under its registered
            //    name. Wrapper types like std::vector<X> / std::optional<X>
            //    are referenced from method param/return slots via the
            //    wrapper's mangled name, but reflect-cpp only emits inner $defs
            //    — without this the $ref dangles and the TS codegen produces
            //    an interface that references unbound symbols.
            if (!contains_key(schemas, name)) {
                rfl::Generic::Object top;
                for (const auto& [k, v] : root) {
                    if (k == "$defs" || k == "$schema") continue;
                    top[k] = v;
                }
                schemas[name] = top;
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
                return this->_codec.write(RpcResponse<rfl::Generic::Object>{
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
                    return this->_codec.write(RpcResponse<rfl::Generic>{
                        .id     = std::move(id),
                        .result = rfl::Generic{},
                    });
                } else {
                    auto ret = detail::invokeMethod<Func, T, ParamTuple, OrigArgs>(
                        _object, args, std::make_index_sequence<N>{});
                    return this->_codec.write(RpcResponse<std::remove_cvref_t<ReturnT>>{
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

    template <auto Func>
    TypedRpcServer& addAsyncMethod(const std::string& name) {
        using Meta       = detail::AsyncFnMetaT<Func>;
        using ParamTuple = std::tuple_element_t<0, Meta>;
        using OrigArgs   = std::tuple_element_t<1, Meta>;
        using ResultT    = std::tuple_element_t<2, Meta>;
        constexpr auto N = std::tuple_size_v<ParamTuple>;

        std::apply([this, &name](auto&&... args) {
            ((addParam<std::remove_cvref_t<decltype(args)>>(name)), ...);
        }, ParamTuple{});

        addReturn<std::remove_cvref_t<ResultT>>(name);

        this->addAsyncHandler(name,
            [this](rfl::Generic id, rfl::Generic params, AsyncContext<C> ctx) {
                auto args_r = rfl::from_generic<ParamTuple>(params);
                if (!args_r) {
                    ctx.respondError(-32602, std::string{args_r.error().what()});
                    return;
                }
                auto args = std::move(args_r).value();

                auto state = std::make_shared<typename Resolver<ResultT>::State>();
                state->onResolve = [this, ctx, id](ResultT v) mutable {
                    // For the QuickJS codec, write() defers JSValue construction
                    // into a thunk materialized on the JS thread at drain time, so
                    // capturing `this` and calling it here off-thread is safe; byte
                    // codecs encode eagerly as before.
                    ctx.respondBytes(this->_codec.write(RpcResponse<ResultT>{
                        .id     = std::move(id),
                        .result = std::move(v),
                    }));
                };
                state->onReject = [ctx](int code, std::string msg) mutable {
                    ctx.respondError(code, std::move(msg));
                };
                Resolver<ResultT> resolver{state};

                try {
                    detail::invokeAsync<Func, T, ParamTuple, OrigArgs>(
                        _object, args, std::move(resolver),
                        std::make_index_sequence<N>{});
                } catch (const std::exception& e) {
                    if (!state->done.exchange(true) && state->onReject) {
                        state->onReject(-32603, e.what());
                    }
                }
            });
        return *this;
    }

    template <auto MemFn>
    TypedRpcServer& addAsyncMethod() {
        constexpr auto name = detail::methodName<MemFn>();
        static_assert(!name.empty(), "Failed to parse method name from function signature");
        return addAsyncMethod<MemFn>(std::string(name));
    }

private:
    T& _object;
    std::unordered_map<std::string, detail::SchemaGenerator> _schemaGenerators;
    std::map<std::string, detail::MethodDesc> _methodSchemas;
};

}  // namespace rpcpp
