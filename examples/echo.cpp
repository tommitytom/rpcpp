// Low-level example: register a raw handler with RpcServer<JsonCodec>.
//
// Run:  echo '{"jsonrpc":"2.0","id":"1","method":"echo","params":{"message":"hi"}}' | ./example_echo

#include "RpcServer.h"
#include "codecs/JsonCodec.h"

int main() {
    rpcpp::RpcServer<rpcpp::JsonCodec> server;

    server.addHandler("echo",
        [](rfl::Generic id, rfl::Generic params) -> std::string {
            auto obj  = params.to_object().value();
            auto msg  = obj.at("message").to_string().value();
            return rfl::json::write(rpcpp::RpcResponse<std::string>{
                .id     = std::move(id),
                .result = std::move(msg),
            });
        });

    server.run();
    return 0;
}
