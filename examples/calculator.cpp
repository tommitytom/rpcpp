// Typed example: register class methods with TypedRpcServer.
//
// Run:  echo '{"jsonrpc":"2.0","id":"1","method":"add","params":[2,3]}' | ./example_calculator

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

    server.run();
    return 0;
}
