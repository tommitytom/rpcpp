#include <sstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "Framer.h"

TEST_CASE("LineFramer roundtrips multiple frames", "[framer][line]") {
    std::stringstream ss;
    rpcpp::LineFramer::write(ss, std::string{"hello"});
    rpcpp::LineFramer::write(ss, std::string{"world"});

    auto a = rpcpp::LineFramer::read<std::string>(ss);
    auto b = rpcpp::LineFramer::read<std::string>(ss);
    auto c = rpcpp::LineFramer::read<std::string>(ss);

    REQUIRE(a.has_value());
    REQUIRE(*a == "hello");
    REQUIRE(b.has_value());
    REQUIRE(*b == "world");
    REQUIRE_FALSE(c.has_value());
}

TEST_CASE("Length32Framer roundtrips and handles empty frames", "[framer][length]") {
    std::stringstream ss;
    const std::vector<char> a{'a', 'b', 'c'};
    const std::vector<char> b{'d', 'e'};
    const std::vector<char> empty;

    rpcpp::Length32Framer::write(ss, a);
    rpcpp::Length32Framer::write(ss, b);
    rpcpp::Length32Framer::write(ss, empty);

    auto x   = rpcpp::Length32Framer::read<std::vector<char>>(ss);
    auto y   = rpcpp::Length32Framer::read<std::vector<char>>(ss);
    auto z   = rpcpp::Length32Framer::read<std::vector<char>>(ss);
    auto eof = rpcpp::Length32Framer::read<std::vector<char>>(ss);

    REQUIRE(x.has_value());
    REQUIRE(*x == a);
    REQUIRE(y.has_value());
    REQUIRE(*y == b);
    REQUIRE(z.has_value());
    REQUIRE(z->empty());
    REQUIRE_FALSE(eof.has_value());
}

TEST_CASE("Length32Framer prefix is big-endian", "[framer][length]") {
    std::stringstream ss;
    const std::vector<char> body(258, 'x');  // 0x0102 bytes
    rpcpp::Length32Framer::write(ss, body);

    const std::string raw = ss.str();
    REQUIRE(raw.size() == 4 + body.size());
    REQUIRE(static_cast<unsigned char>(raw[0]) == 0x00);
    REQUIRE(static_cast<unsigned char>(raw[1]) == 0x00);
    REQUIRE(static_cast<unsigned char>(raw[2]) == 0x01);
    REQUIRE(static_cast<unsigned char>(raw[3]) == 0x02);
}
