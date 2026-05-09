// Discovery example: same as calculator, plus an OpenRPC schema endpoint.
// The schema is also printed to stderr at startup.
//
// Run:  echo '{"jsonrpc":"2.0","id":"1","method":"rpc.discover"}' | ./example_discovery

#include <iostream>

#include "TypedRpcServer.h"
#include "codecs/JsonCodec.h"

class Calculator {
public:
    int add(int a, int b)      { return a + b; }
    int subtract(int a, int b) { return a - b; }
    int multiply(int a, int b) { return a * b; }
};

int main() {
    Calculator calc;
    rpcpp::TypedRpcServer<Calculator, rpcpp::JsonCodec> server(calc);

    server.addMethod<&Calculator::add>();
    server.addMethod<&Calculator::subtract>();
    server.addMethod<&Calculator::multiply>();
    server.addDiscoveryMethod();

    std::cerr << "OpenRPC schema:\n" << server.dumpSchema() << '\n';

    server.run();
    return 0;
}
