//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// UNSUPPORTED: c++03 || c++11 || c++14 || c++17 || c++20
// ADDITIONAL_COMPILE_FLAGS: -freflection

// RUN: %{build}
// RUN: %{exec} %t.exe

#include <meta>
#include <format>
#include <cassert>

#include "test_macros.h"
#include "assert_macros.h"
#include "concat_macros.h"

namespace N {
    struct DebugFormatter {
        constexpr auto parse(auto& ctx) { return ctx.begin(); }

        template <class T>
        auto format(T const& object, auto& ctx) const {
            auto out = ctx.out();
            out = std::format_to(out, "{}{{", display_string_of(^^T));

            auto delim = [first=true, &out]() mutable {
                if (not first) {
                    *out++ = ',';
                    *out++ = ' ';
                }
                first = false;
            };

            constexpr auto ac = std::meta::access_context::unchecked();

            template for (constexpr auto nsdm : define_static_array(nonstatic_data_members_of(^^T, ac))) {
                delim();
                out = std::format_to(out, ".{}={}", identifier_of(nsdm), object.[:nsdm:]);
            }

            *out++ = '}';
            return out;
        }
    };

    struct DeriveDebug {
        consteval auto on_complete(std::meta::info ty) const -> void {
            #if 1
            auto fmt_body = std::meta::list_builder();
            fmt_body += ^^{
                auto out = std::format_to(ctx.out(), "{}{{", \(display_string_of(ty)));
            };

            auto delim = [first=true, &fmt_body]() mutable {
                if (not first) {
                    fmt_body += ^^{
                        *out++ = ',';
                        *out++ = ' ';
                    };
                }
                first = false;
            };

            auto unchecked = std::meta::access_context::unchecked();
            for (auto nsdm : nonstatic_data_members_of(ty, unchecked)) {
                delim();
                fmt_body += ^^{
                    out = std::format_to(out,
                        \(std::meta::str_lit(".", identifier_of(nsdm), "={}")),
                        object.\(nsdm)
                    );
                };
            }

            fmt_body += ^^{
                *out++ = '}';
                return out;
            };

            queue_injection(^^std, ^^{
                template <>
                struct formatter<\(ty)> {
                    constexpr auto parse(auto& ctx) { return ctx.begin(); }

                    auto format(\(ty) const& object, auto& ctx) const {
                        \(fmt_body);
                    }
                };
            });
            #else
            queue_injection(^^std, ^^{
                template <>
                struct formatter<\(ty)> : ::N::DebugFormatter { };
            });
            #endif
        }
    };

    inline constexpr DeriveDebug derive_debug{};
}

struct [[=N::derive_debug]] Config {
    std::string name;
    int amount;
};

template <class T>
struct [[=N::derive_debug]] Widget {
    T thing;
};

// template <> struct std::formatter<Config> : N::DebugFormatter { };

template <class T>
auto test(std::string_view expected, T const& object) -> void {
    if constexpr (std::default_initializable<std::formatter<T>>) {
        std::string out = std::format("{}", object);
        TEST_REQUIRE(
            out == expected,
            TEST_WRITE_CONCATENATED(
                "\nExpected output ", expected,
                "\nActual output   ", out, '\n'));
    } else {
        static_assert(false, "Not formattable");
    }
}

int main() {
    test("Config{.name=abc, .amount=10}", Config{.name="abc", .amount=10});
    test("Widget<int>{.thing=5}", Widget<int>{.thing=5});
}
