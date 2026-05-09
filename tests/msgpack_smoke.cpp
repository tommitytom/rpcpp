// End-to-end smoke test for the msgpack codec.
// Forks example_calculator_msgpack, sends one length-prefixed msgpack add(2,3)
// request, decodes the response, and asserts result == 5.
//
// Usage: test_msgpack_smoke <path-to-example_calculator_msgpack>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <span>
#include <string>
#include <vector>

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <rfl.hpp>
#include <rfl/Generic.hpp>
#include <rfl/msgpack.hpp>

#include "RpcEnvelope.h"

namespace {

bool write_all(int fd, const char* data, std::size_t n) {
    while (n > 0) {
        ssize_t k = ::write(fd, data, n);
        if (k <= 0) return false;
        data += k;
        n    -= static_cast<std::size_t>(k);
    }
    return true;
}

bool read_all(int fd, char* data, std::size_t n) {
    while (n > 0) {
        ssize_t k = ::read(fd, data, n);
        if (k <= 0) return false;
        data += k;
        n    -= static_cast<std::size_t>(k);
    }
    return true;
}

void write_frame(int fd, std::span<const char> bytes) {
    const std::uint32_t len = static_cast<std::uint32_t>(bytes.size());
    const unsigned char hdr[4] = {
        static_cast<unsigned char>((len >> 24) & 0xFF),
        static_cast<unsigned char>((len >> 16) & 0xFF),
        static_cast<unsigned char>((len >> 8)  & 0xFF),
        static_cast<unsigned char>( len        & 0xFF),
    };
    if (!write_all(fd, reinterpret_cast<const char*>(hdr), 4) ||
        !write_all(fd, bytes.data(), bytes.size())) {
        std::cerr << "FAIL: write to child failed: " << std::strerror(errno) << '\n';
        std::exit(1);
    }
}

std::vector<char> read_frame(int fd) {
    unsigned char hdr[4];
    if (!read_all(fd, reinterpret_cast<char*>(hdr), 4)) {
        std::cerr << "FAIL: short read on length prefix\n";
        std::exit(1);
    }
    const std::uint32_t len =
        (static_cast<std::uint32_t>(hdr[0]) << 24) |
        (static_cast<std::uint32_t>(hdr[1]) << 16) |
        (static_cast<std::uint32_t>(hdr[2]) << 8)  |
         static_cast<std::uint32_t>(hdr[3]);
    std::vector<char> buf(len);
    if (len > 0 && !read_all(fd, buf.data(), len)) {
        std::cerr << "FAIL: short read on body (expected " << len << ")\n";
        std::exit(1);
    }
    return buf;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: " << argv[0] << " <path-to-example_calculator_msgpack>\n";
        return 2;
    }
    const char* exe = argv[1];

    rpcpp::RpcRequest req{
        .method  = "add",
        .id      = rfl::Generic{std::string{"1"}},
        .params  = rfl::Generic{rfl::Generic::Array{
                       rfl::Generic{std::int64_t{2}},
                       rfl::Generic{std::int64_t{3}},
                   }},
        .jsonrpc = "2.0",
    };
    const auto req_bytes = rfl::msgpack::write(req);

    int to_child[2];
    int from_child[2];
    if (::pipe(to_child) != 0 || ::pipe(from_child) != 0) {
        std::cerr << "FAIL: pipe(): " << std::strerror(errno) << '\n';
        return 1;
    }

    const pid_t pid = ::fork();
    if (pid < 0) {
        std::cerr << "FAIL: fork(): " << std::strerror(errno) << '\n';
        return 1;
    }

    if (pid == 0) {
        // Child: stdin <- to_child[0], stdout -> from_child[1]
        ::dup2(to_child[0], 0);
        ::dup2(from_child[1], 1);
        ::close(to_child[0]);
        ::close(to_child[1]);
        ::close(from_child[0]);
        ::close(from_child[1]);
        ::execl(exe, exe, static_cast<char*>(nullptr));
        std::cerr << "FAIL: execl(" << exe << "): " << std::strerror(errno) << '\n';
        std::_Exit(127);
    }

    // Parent
    ::close(to_child[0]);
    ::close(from_child[1]);

    write_frame(to_child[1], req_bytes);
    ::close(to_child[1]);  // EOF on child stdin -> child run-loop exits cleanly

    const auto resp_bytes = read_frame(from_child[0]);
    ::close(from_child[0]);

    int status = 0;
    ::waitpid(pid, &status, 0);

    auto resp_r = rfl::msgpack::read<rpcpp::RpcResponse<int>>(resp_bytes);
    if (!resp_r) {
        std::cerr << "FAIL: msgpack::read RpcResponse<int>: " << resp_r.error().what() << '\n';
        return 1;
    }
    const auto& resp = resp_r.value();
    if (resp.result != 5) {
        std::cerr << "FAIL: expected result=5, got " << resp.result << '\n';
        return 1;
    }
    if (resp.jsonrpc != "2.0") {
        std::cerr << "FAIL: expected jsonrpc=\"2.0\", got \"" << resp.jsonrpc << "\"\n";
        return 1;
    }

    std::cout << "PASS: result=" << resp.result << '\n';
    return 0;
}
