#pragma once

#include <array>
#include <cstdint>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rpcpp {

struct LineFramer {
    template <class Buf>
    static std::optional<Buf> read(std::istream& in) {
        Buf buf;
        char ch;
        while (in.get(ch)) {
            if (ch == '\n') return buf;
            buf.push_back(ch);
        }
        if (buf.empty()) return std::nullopt;
        return buf;
    }

    template <class Bytes>
    static void write(std::ostream& out, const Bytes& bytes) {
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        out.put('\n');
        out.flush();
    }
};

struct Length32Framer {
    template <class Buf>
    static std::optional<Buf> read(std::istream& in) {
        std::array<unsigned char, 4> hdr{};
        in.read(reinterpret_cast<char*>(hdr.data()), 4);
        if (in.gcount() != 4) return std::nullopt;
        const std::uint32_t len =
            (static_cast<std::uint32_t>(hdr[0]) << 24) |
            (static_cast<std::uint32_t>(hdr[1]) << 16) |
            (static_cast<std::uint32_t>(hdr[2]) << 8)  |
             static_cast<std::uint32_t>(hdr[3]);
        Buf buf(len, typename Buf::value_type{});
        if (len > 0) {
            in.read(reinterpret_cast<char*>(buf.data()), len);
            if (static_cast<std::uint32_t>(in.gcount()) != len) return std::nullopt;
        }
        return buf;
    }

    template <class Bytes>
    static void write(std::ostream& out, const Bytes& bytes) {
        const auto len = static_cast<std::uint32_t>(bytes.size());
        const unsigned char hdr[4] = {
            static_cast<unsigned char>((len >> 24) & 0xFF),
            static_cast<unsigned char>((len >> 16) & 0xFF),
            static_cast<unsigned char>((len >> 8)  & 0xFF),
            static_cast<unsigned char>( len        & 0xFF),
        };
        out.write(reinterpret_cast<const char*>(hdr), 4);
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
        out.flush();
    }
};

}  // namespace rpcpp
