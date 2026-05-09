// Async example: a method whose answer is computed on a worker thread and
// resolved via Resolver<R>.
//
// Run:  echo '{"jsonrpc":"2.0","id":"1","method":"slow_add","params":[2,3]}' | ./example_async_calc

#include <chrono>
#include <thread>

#include "TypedRpcServer.h"
#include "codecs/JsonCodec.h"
#include "transports/StdioTransport.h"

class Service {
public:
    void slow_add(int a, int b, rpcpp::Resolver<int> r) {
        std::thread([a, b, r = std::move(r)]() mutable {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            r.resolve(a + b);
        }).detach();
    }
};

int main() {
    Service svc;
    rpcpp::StdioTransport<rpcpp::JsonCodec> transport;
    rpcpp::TypedRpcServer<Service, rpcpp::JsonCodec> server(svc, transport);
    server.addAsyncMethod<&Service::slow_add>();
    transport.run(server);
    return 0;
}
