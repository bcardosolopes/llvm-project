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
#include <string_view>

template<typename T>
    requires std::is_enum_v<T>
constexpr std::string_view enum_to_string1(T val)
{
    consteval {
        auto cases = std::meta::token_sequence();
        for (auto e : enumerators_of(^^T)) {
            cases += ^^{
                case \(extract<T>(e)):
                    return \(std::meta::str_lit(identifier_of(e)));
            };
        }

        queue_injection(^^{
            switch (val) {
                \(cases)
            }
        });
    }

    return "<unknown>";
}

template<typename T>
    requires std::is_enum_v<T>
constexpr std::string_view enum_to_string2(T val)
{
    consteval {
        auto cases = std::meta::token_sequence();
        for (auto e : enumerators_of(^^T)) {
            cases += ^^{
                case \(e):
                    return \(identifier_of(e));
            };
        }

        queue_injection(^^{
            switch (val) {
                \(cases)
            }
        });
    }

    return "<unknown>";
}

enum class E { a, b, c, d, e, f };

static_assert(enum_to_string1(E::a) == "a");
static_assert(enum_to_string1(E::d) == "d");
static_assert(enum_to_string1(E(17)) == "<unknown>");

static_assert(enum_to_string2(E::b) == "b");
static_assert(enum_to_string2(E::c) == "c");
static_assert(enum_to_string2(E(17)) == "<unknown>");

int main() { }
