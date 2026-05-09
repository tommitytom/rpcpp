# rpcpp

A small JSON-RPC 2.0 server library for C++20. The server reads requests from `stdin`, dispatches them to registered methods, and writes responses to `stdout`. Method parameters and return values are reflected through [reflect-cpp](https://github.com/getml/reflect-cpp), which also drives the OpenRPC 1.3.2 schema that the optional `rpc.discover` method exposes.

The library is codec-agnostic. JSON is the default, but the same `addMethod<&T::method>()` registrations work over any encoding reflect-cpp supports - msgpack, CBOR, and so on - by swapping a template argument.

## Building

reflect-cpp is a git submodule at `deps/reflect-cpp`; the moodycamel concurrent-queue headers are checked in directly under `deps/moodycamel/`. After cloning:

```
git clone --recurse-submodules <url> rpc++
cmake -S rpc++ -B rpc++/build
cmake --build rpc++/build -j
```

The library target is `rpcpp` (alias `rpcpp::rpcpp`), header-only. Examples are built when rpcpp is the top-level project.

To enable the msgpack codec, set `-DRPCPP_BUILD_MSGPACK=ON` at configure time. This requires `msgpack-c` to be findable by CMake (`find_package(msgpack-c)`); on Debian/Ubuntu, install `libmsgpack-c-dev`.

## Using it

In your own CMake project:

```cmake
add_subdirectory(path/to/rpc++)
target_link_libraries(myapp PRIVATE rpcpp::rpcpp)
```

The typed API is the path of least resistance. Define a class, register member functions by pointer, and run the read/dispatch loop:

```cpp
#include "TypedRpcServer.h"
#include "codecs/JsonCodec.h"

class Calculator {
public:
    int add(int a, int b)      { return a + b; }
    int subtract(int a, int b) { return a - b; }
};

int main() {
    Calculator calc;
    rpcpp::TypedRpcServer<Calculator, rpcpp::JsonCodec> server(calc);
    server.addMethod<&Calculator::add>();
    server.addMethod<&Calculator::subtract>();
    server.run();
}
```

Drive it from a shell:

```
echo '{"jsonrpc":"2.0","id":"1","method":"add","params":[2,3]}' | ./example_calculator
```

The typed server takes positional parameters as a JSON array; the order matches the function signature. The method name is extracted from the function pointer at compile time, so `addMethod<&Calculator::add>()` registers the method as `"add"`. Pass an explicit name to `addMethod<F>("alias")` if you prefer.

## Codecs

A codec is a small policy struct that tells the server how to read and write bytes. `JsonCodec` is built in (`src/codecs/JsonCodec.h`); `MsgpackCodec` is opt-in (`src/codecs/MsgpackCodec.h`, requires `RPCPP_BUILD_MSGPACK=ON`). Swapping codecs is a one-line change:

```cpp
rpcpp::TypedRpcServer<Calculator, rpcpp::MsgpackCodec> server(calc);
```

Each codec also picks a default framing strategy. JSON uses newline-delimited frames; msgpack uses a 4-byte big-endian length prefix. Both are exposed as `rpcpp::LineFramer` and `rpcpp::Length32Framer` and can be overridden via the third and fourth template arguments to `RpcServer` / `TypedRpcServer`.

Adding a new codec is roughly a dozen lines: declare the input/output/buffer types and forward `read<T>` and `write` to the matching `rfl::<format>::read`/`write`. See `src/codecs/JsonCodec.h` for the template.

## Two API levels

`TypedRpcServer<T, Codec>` is the typed wrapper described above. Parameter parsing and result serialization are generated for you, and every registered method is recorded in a schema you can emit on demand.

`RpcServer<Codec>` is the lower-level base class. You register handlers with `addHandler(name, fn)` where `fn` receives the request `id` and `params` as `rfl::Generic` and returns the encoded response bytes. Use this for loose parameter shapes, custom dispatch, or anywhere the typed path is in the way. See `examples/echo.cpp`.

## Async dispatch

For methods whose answers come from a worker thread, an external service, or any path that can't return inline, register them with `addAsyncMethod` and take a `rpcpp::Resolver<R>` as the trailing parameter. The method returns `void` immediately; whoever ends up with the resolver calls `r.resolve(value)` (or `r.reject(code, msg)`) from any thread.

```cpp
class Service {
public:
    void slow_add(int a, int b, rpcpp::Resolver<int> r) {
        std::thread([a, b, r = std::move(r)]() mutable {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            r.resolve(a + b);
        }).detach();
    }
};

server.addAsyncMethod<&Service::slow_add>();
```

`Resolver<R>` is copyable (the underlying state is shared) and idempotent - only the first `resolve`/`reject` wins. If every copy is dropped without firing, the library auto-emits `-32603 "Async request was abandoned"` so a client never silently hangs. `RpcServer::run()` waits for in-flight async work to drain before exiting, which keeps test scripts and stdio clients from missing the tail of a response.

For codec-level control there's a raw counterpart: `server.addAsyncHandler(name, fn)` where `fn` receives `(id, params, AsyncContext<C> ctx)` and calls `ctx.respondBytes(...)` / `ctx.respondError(...)` once the work completes. Useful when reflect-cpp parsing is in the way or the response shape is dynamic. See [examples/async_calc.cpp](examples/async_calc.cpp).

The OpenRPC schema for an async method is identical to the sync equivalent - async-ness is a server-side dispatch detail, not part of the contract.

## Discovery

Calling `server.addDiscoveryMethod()` registers `rpc.discover`, which returns the server's OpenRPC document so clients can introspect available methods, parameters, and return types. `dumpSchema()` returns the same document as a pretty-printed JSON string regardless of the active codec - OpenRPC documents are conventionally JSON, and this gives you a stable schema artifact even when the wire format is binary. See `examples/discovery.cpp`.

## Cross-platform notes

The library and tests are portable across Linux, macOS, and Windows. The typed method-name extraction has a separate code path for MSVC's `__FUNCSIG__`, the framing helpers and run loop go through `std::istream` / `std::ostream`, and tests drive everything in-process via `std::stringstream`.

On Windows, binary codecs need stdin/stdout in binary mode or CRLF translation will corrupt frames. `RpcServer::run()` calls `rpcpp::set_binary_stdio()` automatically when the codec advertises `is_binary == true`. If you drive `processMessage` against `std::cin`/`std::cout` yourself, call `rpcpp::set_binary_stdio()` once at startup (defined in `Stdio.h`; a no-op on POSIX).

## Requirements

C++20 compiler, CMake 3.23 or newer. The msgpack codec additionally requires `msgpack-c` (Debian/Ubuntu: `libmsgpack-c-dev`). Set `-DRPCPP_BUILD_TESTS=OFF` to skip the test targets when consuming via `add_subdirectory`.
