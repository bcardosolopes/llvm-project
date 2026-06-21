//===----------------------------------------------------------------------===//
//
// Copyright 2025 Bloomberg Finance L.P.
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// UNSUPPORTED: c++03 || c++11 || c++14 || c++17 || c++20
// ADDITIONAL_COMPILE_FLAGS: -freflection

// <experimental/reflection>
//
// members_of (and the queries built on it) work while a class is still being
// defined: they observe the members declared so far. This enables injecting
// members conditioned on the members already present (e.g. completing an
// iterator interface based on what the author provided).

// RUN: %{build}
// RUN: %{exec} %t.exe

#include <meta>
#include <cassert>

using std::meta::access_context;

                  // =====================================
                  // introspection while being defined
                  // =====================================

struct C {
  int a;
  int b;
  // members_of sees the members declared so far, mid-definition.
  static_assert(nonstatic_data_members_of(^^C, access_context::current()).size() == 2);
  int c;
  static_assert(nonstatic_data_members_of(^^C, access_context::current()).size() == 3);
};
// ...and the same query gives the same answer once the class is complete.
static_assert(nonstatic_data_members_of(^^C, access_context::current()).size() == 3);


                  // =====================================
                  // inject members conditioned on present members
                  // =====================================

struct Iterator {
  int value;
  using difference_type = long;

  consteval {
    // Introspect the members provided by the author so far.
    auto dms = nonstatic_data_members_of(^^Iterator, access_context::current());

    // Inject the rest of the interface, conditioned on what is present.
    queue_injection(^^{ static constexpr int data_member_count = \(dms.size()); });
    if (dms.size() == 1)
      queue_injection(^^{ static constexpr bool single_field = true; });
  }
};

// Only `value` is a nonstatic data member when the consteval block runs
// (`difference_type` is a typedef), so the conditional injection fires.
static_assert(Iterator::data_member_count == 1);
static_assert(Iterator::single_field == true);


                  // =====================================
                  // bases_of mid-definition (+ recursing into indirect members)
                  // =====================================

struct Base1 { int x; int y; };
struct Base2 { int z; };

struct Derived : Base1, Base2 {
  int w;

  consteval {
    auto ctx = access_context::current();
    auto bases = bases_of(^^Derived, ctx);

    // Recurse through the (already-known) bases to count direct and indirect
    // nonstatic data members while Derived is still being defined.
    auto total = nonstatic_data_members_of(^^Derived, ctx).size();
    for (auto b : bases)
      total += nonstatic_data_members_of(type_of(b), ctx).size();

    queue_injection(^^{ static constexpr int base_count = \(bases.size()); });
    queue_injection(^^{ static constexpr int total_fields = \(total); });
  }
};

static_assert(Derived::base_count == 2);    // Base1, Base2
static_assert(Derived::total_fields == 4);  // x, y (Base1) + z (Base2) + w


                  // =====================================
                  // inject a self-returning member into a class *template*
                  // =====================================

// Injecting a member function whose signature references the enclosing type is
// the tricky case during instantiation: the type is still incomplete at the
// point of injection. The injected body must be deferred until the
// specialization is complete. Here 'minus' returns the (incomplete) Scaled<B>
// and is injected only when the class lacks a usable 'minus' already.

template <bool HasMinus>
struct Scaled {
  int n;
  constexpr auto plus(int d) const -> Scaled { return Scaled{n + d}; }
  constexpr auto minus(int d) const -> Scaled requires HasMinus {
    return Scaled{n - d};
  }

  consteval {
    bool has_plus = false, has_minus = false;
    for (auto m : members_of(^^Scaled, access_context::current())) {
      if (!has_identifier(m))
        continue;
      if (identifier_of(m) == "plus") has_plus = true;
      else if (identifier_of(m) == "minus") has_minus = true;
    }
    if (has_plus && !has_minus)
      queue_injection(^^{
        constexpr auto minus(int d) const -> Scaled { return plus(-d); }
      });
  }
};

static_assert(Scaled<true>{5}.minus(2).n == 3);   // uses the declared 'minus'
static_assert(Scaled<false>{5}.minus(2).n == 3);  // uses the injected 'minus'


                  // =====================================
                  // defer all late-parsed injected pieces in templates
                  // =====================================

template <bool B>
struct CompleteSensitive {
  consteval {
    queue_injection(^^{
      static constexpr auto default_arg(decltype(sizeof(0)) n =
                                            sizeof(CompleteSensitive))
          -> decltype(sizeof(0)) {
        return n;
      }
      static constexpr auto noexcept_value()
          noexcept(sizeof(CompleteSensitive) > 0) -> bool {
        return true;
      }
      decltype(sizeof(0)) bytes = sizeof(CompleteSensitive);
    });
  }
};

static_assert(CompleteSensitive<false>::default_arg() ==
              sizeof(CompleteSensitive<false>));
static_assert(noexcept(CompleteSensitive<false>::noexcept_value()));
static_assert(CompleteSensitive<false>{}.bytes ==
              sizeof(CompleteSensitive<false>));


int main() {
  Iterator it{42};
  assert(it.value == 42);
  Derived d{};
  assert(Derived::total_fields == 4);
  (void)d;
  return 0;
}
