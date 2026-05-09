// Discovery example: same as calculator, plus an OpenRPC schema endpoint.
// The schema is also printed to stderr at startup.
//
// Run:  echo '{"jsonrpc":"2.0","id":"1","method":"rpc.discover"}' | ./example_discovery

#include <iostream>

#include "TypedRpcServer.h"
#include "codecs/JsonCodec.h"
#include "transports/StdioTransport.h"

struct Vec2 {
    double x;
    double y;
};

class Calculator {
public:
    int  add(int a, int b)            { return a + b; }
    int  subtract(int a, int b)       { return a - b; }
    int  multiply(int a, int b)       { return a * b; }
    Vec2 translate(Vec2 p, Vec2 delta) { return { p.x + delta.x, p.y + delta.y }; }
};

int main() {
    Calculator calc;
    rpcpp::StdioTransport<rpcpp::JsonCodec> transport;
    rpcpp::TypedRpcServer<Calculator, rpcpp::JsonCodec> server(calc, transport);

    server.addMethod<&Calculator::add>();
    server.addMethod<&Calculator::subtract>();
    server.addMethod<&Calculator::multiply>();
    server.addMethod<&Calculator::translate>();
    server.addDiscoveryMethod();

    std::cerr << "OpenRPC schema:\n" << server.dumpSchema() << '\n';

    transport.run(server);
    return 0;
}
