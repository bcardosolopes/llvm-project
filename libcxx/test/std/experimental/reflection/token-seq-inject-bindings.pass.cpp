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

// Push-based customization of structured bindings: an annotation whose
// on_template_defined callback fires once, when the annotated class template's
// definition completes, and injects partial specializations of std::tuple_size
// and std::tuple_element covering every specialization; its inject_members
// callback fires right before the pattern is completed and injects get() as a
// member (an explicit object function template), so no namespace-scope get,
// no ADL, and no constraint machinery are needed. Unlike per-instantiation
// injection of explicit specializations, the injected entities exist before
// any specialization is instantiated, so the protocol is order-independent.

#include <meta>
#include <cassert>
#include <concepts>
#include <utility>

#include "test_macros.h"

using std::meta::info;

// ------------------------- the library -------------------------
namespace lib {
    struct inject_bindings_t {
        consteval auto on_template_defined(info tmpl) const -> void {
            queue_injection(^^std, ^^{
                template <class... Ts>
                    requires requires { \(tmpl)<Ts...>::tuple_elements; }
                struct tuple_size<\(tmpl)<Ts...>>
                    : integral_constant<size_t, size(\(tmpl)<Ts...>::tuple_elements)>
                { };

                template <size_t I, class... Ts>
                    requires requires { \(tmpl)<Ts...>::tuple_elements; }
                struct tuple_element<I, \(tmpl)<Ts...>> {
                    using type = [: type_of(\(tmpl)<Ts...>::tuple_elements[I]) :];
                };
            });
        }

        consteval auto inject_members(info) const -> std::meta::token_sequence {
            return ^^{
               public:
                template <std::size_t I, class Self>
                constexpr auto get(this Self&& self) -> decltype(auto) {
                    // Parenthesized: decltype(auto) must deduce a reference,
                    // not the member's declared type.
                    return (((Self&&)self).[: tuple_elements[I] :]);
                }
            };
        }
    };
    inline constexpr inject_bindings_t inject_bindings{};
}

// ------------------------- the user code -------------------------
template <class T>
class [[=lib::inject_bindings]] wide_result {
    T hi;
    T lo;

public:
    constexpr wide_result(T hi, T lo) : hi(hi), lo(lo) { }

    static constexpr info tuple_elements[] = {^^hi, ^^lo};
};

// The whole point: the protocol is complete before any specialization of
// wide_result has been instantiated.
static_assert(std::tuple_size_v<wide_result<int>> == 2);
static_assert(std::same_as<std::tuple_element_t<0, wide_result<unsigned>>, unsigned>);
static_assert(std::same_as<std::tuple_element_t<1, wide_result<unsigned>>, unsigned>);

// A namespace-nested template with heterogeneous members, and a second
// opted-in template whose injected bridges must coexist with the first's.
namespace app {
    template <class T, class U>
    class [[=lib::inject_bindings]] pair_ish {
        T first;
        U second;
    public:
        constexpr pair_ish(T t, U u) : first(t), second(u) { }
        static constexpr info tuple_elements[] = {^^first, ^^second};
    };

    template <class T>
    struct [[=lib::inject_bindings]] single {
        T only;
        static constexpr info tuple_elements[] = {^^only};
    };
}

static_assert(std::tuple_size_v<app::pair_ish<int, char>> == 2);
static_assert(std::same_as<std::tuple_element_t<1, app::pair_ish<int, char>>, char>);
static_assert(std::tuple_size_v<app::single<double>> == 1);

// A template that doesn't opt in is not tuple-like; no hard error from the
// injected (constrained) bridges.
template <class T> class not_opted_in { T x; };
template <class T> concept tuple_like = requires { std::tuple_size<T>::value; };
static_assert(!tuple_like<not_opted_in<int>>);
static_assert(!tuple_like<int>);
static_assert(tuple_like<wide_result<char>>);

int main(int, char**) {
    auto [hi, lo] = wide_result<unsigned long>(123, 456);
    assert(hi == 123);
    assert(lo == 456);

    // Binding to a reference; mutation goes through get's forwarding.
    auto wr = wide_result<int>(1, 2);
    auto& [h2, l2] = wr;
    h2 = 10;
    assert(wr.get<0>() == 10);
    assert(std::move(wr).get<1>() == 2);

    auto [a, b] = app::pair_ish<int, char>(7, 'x');
    assert(a == 7);
    assert(b == 'x');

    auto [only] = app::single<double>{3.5};
    assert(only == 3.5);

    return 0;
}
