#pragma once

#include <span>
#include <vector>

#include <rfl.hpp>
#include <rfl/msgpack.hpp>

#include "../Framer.h"

namespace rpcpp {

struct MsgpackCodec {
    using input_t  = std::span<const char>;
    using output_t = std::vector<char>;
    using buffer_t = std::vector<char>;
    using default_in_framer  = Length32Framer;
    using default_out_framer = Length32Framer;

    template <class T>
    static auto read(input_t bytes) {
        return rfl::msgpack::read<T>(bytes.data(), bytes.size());
    }

    template <class T>
    static output_t write(const T& value) {
        return rfl::msgpack::write(value);
    }
};

}  // namespace rpcpp
