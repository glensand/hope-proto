/* Copyright (C) 2023 - 2024 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/daedalus-proto-lib
 */

#pragma once

#include <sstream>
#include <cstdint>

class mock_stream final {
public:
    mock_stream() = default;

    void write(const void* data, std::size_t length) {
        stream_impl.write((const char*)data, length);
    }

    size_t read(void* data, std::size_t length) {
        stream_impl.read((char*)data, length);
        return length;
    }

    template<typename TValue>
    void write(const TValue &val) {
        static_assert(std::is_trivial_v<std::decay_t<TValue>>,
                      "write(const TValue&) is only available for trivial types");
        write(&val, sizeof(val));
    }

    template<typename TValue>
    void read(TValue& val) {
        static_assert(std::is_trivial_v <std::decay_t<TValue>> ,
                      "read() is only available for trivial types");
        read(&val, sizeof(val));
    }

    template<typename TValue>
    TValue read() {
        TValue val;
        read(val);
        return val;
    }

private:
    std::stringstream stream_impl;
};

template <>
inline void mock_stream::read<std::string>(std::string& val) {
    const auto size = read<uint16_t>();
    if (size > 0) { 
        val.resize(size);
        read(val.data(), size);
    }
}

template <>
inline void mock_stream::write<std::string>(const std::string& val) {
    write((uint16_t)val.size());
    write(val.c_str(), val.size());
}

#include "hope_proto/hope_proto.h"

using int32 = hope::proto::int32<mock_stream>;
using uint64 = hope::proto::uint64<mock_stream>;
using float64 = hope::proto::float64<mock_stream>;
using string = hope::proto::string<mock_stream>;
using argument_struct = hope::proto::argument_struct<mock_stream>;

template<typename TValue>
using array = hope::proto::array<mock_stream, TValue>;
