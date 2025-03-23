/* Copyright (C) 2023 - 2025 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/hope-proto
 */

#pragma once

#include <type_traits>
#include <string>
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <cassert>
#include <algorithm>
#include <functional>

namespace hope::proto {

    enum class e_argument_type : uint8_t {
        int32,
        uint64,
        float64,
        string,
        array,
        struct_value,
        file,
        blob,
        count
    };

    template<typename TStream>
    class argument {
    public:
        explicit argument(e_argument_type in_type)
            : argument_type(in_type){}

        argument(std::string in_name, e_argument_type in_type)
            : name(std::move(in_name))
            , argument_type(in_type){}

        virtual ~argument() = default;

        [[nodiscard]] const std::string& get_name() const { return name; }
        [[nodiscard]] e_argument_type get_type() const { return argument_type; }

        template<typename TValue>
        [[nodiscard]] const TValue& as() const { return *(TValue*)get_value_internal(); }

        virtual void write(TStream& stream) {
            stream.write(argument_type);
            stream.write(name);
            write_value(stream);
        }

        virtual void read(TStream& stream) {
            stream.read(name);
            read_value(stream);
        }

    protected:
        virtual void write_value(TStream& stream) = 0;
        virtual void read_value(TStream& stream) = 0;
        [[nodiscard]] virtual void* get_value_internal() const = 0;

        std::string name;
        e_argument_type argument_type;
    };

    template<typename TStream>
    class argument_blob final : public argument<TStream> {
    public:
        argument_blob()
            : argument<TStream>(e_argument_type::blob){}

        argument_blob(std::string&& in_name, std::vector<uint8_t>&& blob)
                : argument<TStream>(std::move(in_name), e_argument_type::blob)
                , m_blob(std::move(blob)) {}

        auto&& get_buffer() { return m_blob; }
    private:
        virtual void write_value(TStream& stream) override {
            stream.template write(uint32_t(m_blob.size()));
            stream.write(m_blob.data(), m_blob.size());
        }

        virtual void read_value(TStream& stream) override {
            const auto size = stream.template read<uint32_t>();
            m_blob.resize(size);
            stream.read(m_blob.data(), m_blob.size());
        }

        [[nodiscard]] virtual void* get_value_internal() const override {
            return (void*)&m_blob;
        }

        std::vector<uint8_t> m_blob;
    };

    template<typename TStream, typename TValue, e_argument_type Type>
    class argument_generic : public argument<TStream> {
        constexpr static bool is_trivial = std::is_trivial_v<TValue> || std::is_same_v<std::string, TValue>;
    public:

        constexpr static e_argument_type type = Type;

        argument_generic()
            : argument<TStream>(Type){}

        argument_generic(std::string&& in_name, TValue&& in_val)
                : argument<TStream>(std::move(in_name), Type)
                , val(std::move(in_val)) {}

        argument_generic(std::string&& in_name, const TValue& in_val)
                : argument<TStream>(std::move(in_name), Type)
                , val(in_val) {}

        [[nodiscard]] const TValue& get() const { return val; }

    protected:
        virtual void write_value(TStream& stream) override {
            assert(is_trivial);
            if constexpr(is_trivial) {
                stream.write(val);
            }
        }

        virtual void read_value(TStream& stream) override {
            assert(is_trivial);
            if constexpr(is_trivial) {
                stream.read(val);
            }
        }

        [[nodiscard]] virtual void* get_value_internal() const override {
            return (void*)(&val);
        }

        TValue val;
    };

    template<typename TStream>
    hope::proto::argument<TStream>* serialize(TStream& stream);

    template<typename TStream>
    class argument_struct final : public argument<TStream> {
    public:
        constexpr static e_argument_type type = e_argument_type::struct_value;

        argument_struct()
            : argument<TStream>(e_argument_type::struct_value){}

        virtual ~argument_struct() override {
            for (auto* v : values)
                delete v;
        }

        virtual void write_value(TStream& stream) override {
            write_values(stream);
        }

        virtual void read_value(TStream& stream) override {
            read_values(stream);
        }

        template<typename T>
        const T& field(const std::string& name) {
            argument<TStream>* arg{ nullptr };
            for (auto* candidate : values){
                if (candidate->get_name() == name){
                    arg = candidate;
                    break;
                }
            }
            return arg->template as<T>();
        }

        auto release(const std::string& name) {
            argument<TStream>* res{ nullptr };
            auto it = std::remove_if(begin(values), end(values), 
                [&](const auto* arg) { return arg->get_name() == name; });
            if (it != end(values)) {
                res = *it;
                values.erase(it);
            }
            return res;
        }

        void release(argument<TStream>* in_argument) {
            values.erase(std::remove(begin(values), end(values), in_argument));
        }

        argument_struct(std::string&& in_name, std::vector<argument<TStream>*>&& args)
            : argument<TStream>(std::move(in_name), e_argument_type::struct_value)
            , values(std::move(args)){

        }

    private:

        void write_values(TStream& stream) const {
            stream.write(values.size());
            for (auto* v : values){
                v->write(stream);
            }
        }

        void read_values(TStream& stream) {
            const auto size = stream.template read<std::size_t>();
            for (std::size_t i{ 0 }; i < size; ++i){
                values.emplace_back(serialize(stream));
            }
        }

        [[nodiscard]] virtual void* get_value_internal() const override {
            return (void*)&values;
        }

        std::vector<argument<TStream>*> values;
    };

    template<typename TStream>
    class struct_builder final {
    public:
        static struct_builder create() {
            return struct_builder{};
        }

        template<typename TValue, typename... Ts>
        struct_builder& add(Ts&&... args) {
            values.emplace_back(new TValue(std::forward<Ts>(args)...));
            return *this;
        }

        struct_builder& add(argument<TStream>* in_argument) {
            if (in_argument) {
                values.emplace_back(in_argument);
            }
            return *this;
        }

        argument_struct<TStream>* get(std::string&& name) {
            return new argument_struct<TStream>(std::move(name), std::move(values));
        }
 
        virtual ~struct_builder(){
            assert(values.empty());
        }
    private:
        std::vector<argument<TStream>*> values;
        struct_builder() = default;
    };

 template<typename TStream, typename TValue>
    class array final : public argument_generic<TStream, std::vector<TValue>, e_argument_type::array> {
        static_assert(
                std::is_same_v<double, TValue>
                || std::is_same_v<int32_t , TValue>
                || std::is_same_v<uint64_t , TValue>
                || std::is_same_v<std::string, TValue>
                || std::is_base_of_v<argument<TStream>, std::remove_pointer_t<TValue>>,
                "Only specified types are allowed"
            );

        using base = argument_generic<TStream, std::vector<TValue>, e_argument_type::array>;
        constexpr static bool is_trivial = !std::is_base_of_v<argument<TStream>, std::remove_pointer_t<TValue>>;
    public:

        explicit array() = default;
        array(std::string in_name, std::vector<TValue> in_value)
                : base(std::move(in_name), std::move(in_value)) {
            
        }

        virtual ~array() override {
            if constexpr (std::is_same_v<TValue, argument<TStream>*>){
                for (auto v : base::val)
                    delete v;
            }
        }

        virtual void write(TStream& stream) override {
            stream.write(argument<TStream>::argument_type);
            stream.write(array_value_type);
            stream.write(argument<TStream>::name);
            write_value(stream);
        }

    private:
        constexpr static e_argument_type get_type()  {
            using clear_t = std::decay_t<TValue>;
            if constexpr (std::is_same_v<clear_t, int32_t>)
                return e_argument_type::int32;
            if constexpr (std::is_same_v<clear_t, uint64_t>)
                return e_argument_type::uint64;
            if constexpr (std::is_same_v<clear_t, std::string>)
                return e_argument_type::string;
            if constexpr (std::is_base_of_v<argument<TStream>, TValue>)
                return std::remove_all_extents_t<TValue>::type;
            if constexpr (std::is_same_v<clear_t, double>)
                return e_argument_type::float64;
            return e_argument_type::count;
        }

        virtual void write_value(TStream& stream) override {
            stream.write((std::size_t)base::val.size());
            for (const auto& v : base::val) {
                if constexpr(is_trivial){
                    stream.write(v);
                }
                if constexpr (!is_trivial){
                    v->write_value(stream);
                }
            }
        }

        virtual void read_value(TStream& stream) override {
            auto size = stream.template read<std::size_t>();
            base::val.reserve(size);
            for (std::size_t i { 0 }; i < size; ++i){
                if constexpr (is_trivial) {
                    auto v = stream.template read<TValue>();
                    base::val.emplace_back(v);
                }
                if constexpr (!is_trivial) {
                    auto* argument = new std::remove_pointer_t<TValue>();
                    base::val.emplace_back(argument);
                    argument->read_value(stream);
                }
            }
        }

        e_argument_type array_value_type{ get_type() };
    };

    template<typename TStream>
    using int32 = argument_generic<TStream, int32_t, e_argument_type::int32>;
    template<typename TStream>
    using uint64 = argument_generic<TStream, uint64_t, e_argument_type::uint64>;
    template<typename TStream>
    using float64 = argument_generic<TStream, double, e_argument_type::float64>;
    template<typename TStream>
    using string = argument_generic<TStream, std::string, e_argument_type::string>;

    template<typename TStream>
    hope::proto::argument<TStream>* serialize(TStream& stream) {
        using factory_impl_t = std::unordered_map<e_argument_type, std::function<argument<TStream>*(TStream&)>>;
        static factory_impl_t factory_impl;
        if (factory_impl.empty()) {
            auto register_type = [](auto type, auto v) {
                using arg_t = std::decay_t<decltype(v)>;
                factory_impl.emplace(type, [] (TStream&) { return new arg_t(); });
            };

            // TODO:: use something like type-map
            register_type(e_argument_type::int32, int32<TStream>{});
            register_type(e_argument_type::float64, float64<TStream>{});
            register_type(e_argument_type::string, string<TStream>{});
            register_type(e_argument_type::uint64, uint64<TStream>{});
            register_type(e_argument_type::struct_value, argument_struct<TStream>{});
            register_type(e_argument_type::blob, argument_blob<TStream>{});

            factory_impl.emplace(e_argument_type::array, [](TStream& stream)-> argument<TStream>* {
                auto sub_type = stream.template read<e_argument_type>();
                if (sub_type == e_argument_type::int32)
                    return new array<TStream, int32_t>{};
                if (sub_type == e_argument_type::float64)
                    return new array<TStream, double>{};
                if (sub_type == e_argument_type::string)
                    return new array<TStream, std::string>{};
                if (sub_type == e_argument_type::uint64)
                    return new array<TStream, uint64_t>{};
                if (sub_type == e_argument_type::struct_value)
                    return new array<TStream, argument_struct<TStream>*>{};
                return nullptr;
            });
        }

        auto type = stream.template read<e_argument_type>();
        auto* new_argument = factory_impl[type](stream);
        assert(new_argument);
        new_argument->read(stream);
        return new_argument;
    }

}
