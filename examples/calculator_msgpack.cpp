// Msgpack example: same Calculator class, swap JsonCodec for MsgpackCodec.
//
// Wire framing is length-prefixed (Length32Framer): a 4-byte big-endian length
// followed by the msgpack-encoded payload. Drive with a small client that
// constructs/decodes msgpack frames.

#include "TypedRpcServer.h"
#include "codecs/MsgpackCodec.h"
#include "transports/StdioTransport.h"

class Calculator {
public:
    int add(int a, int b)      { return a + b; }
    int subtract(int a, int b) { return a - b; }
    int multiply(int a, int b) { return a * b; }
};

int main() {
    Calculator calc;
    rpcpp::StdioTransport<rpcpp::MsgpackCodec> transport;
    rpcpp::TypedRpcServer<Calculator, rpcpp::MsgpackCodec> server(calc, transport);

    server.addMethod<&Calculator::add>();
    server.addMethod<&Calculator::subtract>();
    server.addMethod<&Calculator::multiply>();

    transport.run(server);
    return 0;
}
