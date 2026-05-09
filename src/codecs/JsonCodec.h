#pragma once

#include <string>
#include <string_view>

#include <rfl.hpp>
#include <rfl/json.hpp>

#include "../Framer.h"

namespace rpcpp {

struct JsonCodec {
    using input_t  = std::string_view;
    using output_t = std::string;
    using buffer_t = std::string;
    using default_in_framer  = LineFramer;
    using default_out_framer = LineFramer;
    static constexpr bool is_binary = false;

    template <class T>
    static auto read(input_t bytes) {
        return rfl::json::read<T>(bytes);
    }

    template <class T>
    static output_t write(const T& value) {
        return rfl::json::write(value);
    }
};

}  // namespace rpcpp
