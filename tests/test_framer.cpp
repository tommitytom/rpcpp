// Cross-platform framer tests (no fork, no real I/O).

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "Framer.h"

namespace {

#define REQUIRE(cond)                                                       \
    do {                                                                    \
        if (!(cond)) {                                                      \
            std::cerr << "FAIL: " #cond " at " << __FILE__ << ':'           \
                      << __LINE__ << '\n';                                  \
            std::exit(1);                                                   \
        }                                                                   \
    } while (0)

void test_line_framer() {
    std::stringstream ss;
    rpcpp::LineFramer::write(ss, std::string{"hello"});
    rpcpp::LineFramer::write(ss, std::string{"world"});

    auto a = rpcpp::LineFramer::read<std::string>(ss);
    auto b = rpcpp::LineFramer::read<std::string>(ss);
    auto c = rpcpp::LineFramer::read<std::string>(ss);
    REQUIRE(a.has_value() && *a == "hello");
    REQUIRE(b.has_value() && *b == "world");
    REQUIRE(!c.has_value());
}

void test_length32_framer_roundtrip() {
    std::stringstream ss;
    const std::vector<char> a{'a', 'b', 'c'};
    const std::vector<char> b{'d', 'e'};
    const std::vector<char> empty{};

    rpcpp::Length32Framer::write(ss, a);
    rpcpp::Length32Framer::write(ss, b);
    rpcpp::Length32Framer::write(ss, empty);

    auto x = rpcpp::Length32Framer::read<std::vector<char>>(ss);
    auto y = rpcpp::Length32Framer::read<std::vector<char>>(ss);
    auto z = rpcpp::Length32Framer::read<std::vector<char>>(ss);
    auto eof = rpcpp::Length32Framer::read<std::vector<char>>(ss);
    REQUIRE(x.has_value() && *x == a);
    REQUIRE(y.has_value() && *y == b);
    REQUIRE(z.has_value() && z->empty());
    REQUIRE(!eof.has_value());
}

void test_length32_framer_endianness() {
    std::stringstream ss;
    const std::vector<char> body(258, 'x');  // 0x0102 bytes
    rpcpp::Length32Framer::write(ss, body);

    const std::string raw = ss.str();
    REQUIRE(raw.size() == 4 + 258);
    // Big-endian length prefix: 00 00 01 02
    REQUIRE(static_cast<unsigned char>(raw[0]) == 0x00);
    REQUIRE(static_cast<unsigned char>(raw[1]) == 0x00);
    REQUIRE(static_cast<unsigned char>(raw[2]) == 0x01);
    REQUIRE(static_cast<unsigned char>(raw[3]) == 0x02);
}

}  // namespace

int main() {
    test_line_framer();
    test_length32_framer_roundtrip();
    test_length32_framer_endianness();
    std::cout << "PASS\n";
    return 0;
}
