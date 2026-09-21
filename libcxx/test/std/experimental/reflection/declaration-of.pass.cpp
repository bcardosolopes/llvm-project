//===----------------------------------------------------------------------===//
//
// Copyright 2026 Jump Trading, LLC
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// UNSUPPORTED: c++03 || c++11 || c++14 || c++17 || c++20
// ADDITIONAL_COMPILE_FLAGS: -freflection -std=c++2d

// RUN: %{build}
// RUN: %{exec} %t.exe

// std::meta::declaration_of + std::meta::forwarding_call_for: clone member
// function declarations (including member templates with their heads and
// constraints) into a wrapper class, with generated forwarding bodies.
//
// This recovers what the per-name deducing-this forwarder loses (see
// logging-vector.pass.cpp): explicit template arguments, braced-init-list
// arguments, real parameter types and default arguments, and named
// constraints.

#include <meta>
#include <concepts>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#define CHECK(...) do { if (!(__VA_ARGS__)) return 0; } while (false)

// ----------------------------------------------------------------------------
// The wrapped class: one of each interesting shape.
// ----------------------------------------------------------------------------
struct Pair { int a; int b; };

struct Impl {
  int state = 100;

  // Concrete member; by-value and by-reference parameters.
  constexpr int plain(int x, const int& y) { return state + x + y; }

  // Default argument; const-qualified.
  constexpr int defaulted(int a, int b = 40) const { return a + b; }

  // Braced-init-list argument needs a real parameter type.
  constexpr int take(Pair p) { return p.a + p.b; }

  // Variadic member template.
  template <class... Args>
  constexpr int sum(Args... args) { return (0 + ... + args); }

  // Explicit NTTP argument: impossible for a deducing-this forwarder.
  template <std::size_t N>
  constexpr int get() const { return int(N) * 10; }

  // Mixed template parameter kinds, explicitly supplied.
  template <class T, int N>
  constexpr T scale(T v) const { return v * N; }

  // Constrained (trailing requires).
  template <class T>
  constexpr T only_int(T v) requires std::integral<T> { return v; }

  // Constrained parameter (type-constraint spelling).
  template <std::integral T>
  constexpr T twice(T v) { return v + v; }

  // Ref-qualified.
  constexpr int lref() & { return 1; }
  constexpr int rref() && { return 2; }

  // An overload set: each declaration clones separately.
  constexpr int pick(int) { return 1; }
  constexpr int pick(double) { return 2; }
};

// ----------------------------------------------------------------------------
// The generator.
// ----------------------------------------------------------------------------
consteval auto wrapper_members_of(std::meta::info U) -> std::meta::token_sequence {
  auto v = members_of(U, std::meta::access_context::current());
  std::erase_if(v, [](std::meta::info m) {
    if (is_function_template(m))
      return false;
    return not is_function(m) or is_special_member_function(m) or
           is_static_member(m);
  });

  std::meta::token_sequence out = ^^{};
  for (std::meta::info m : v) {
    auto d = declaration_of(m);
    auto call = forwarding_call_for(d, ^^{ impl });
    out += ^^{
      \(d) {
        ++calls;
        return \(call);
      }
    };
  }
  return out;
}

struct Logged {
  Impl impl;
  mutable int calls = 0;

  consteval {
    queue_injection(wrapper_members_of(^^Impl));
  }
};

// ----------------------------------------------------------------------------
// Behavior: everything forwards, and every call is counted.
// ----------------------------------------------------------------------------
constexpr int use_all() {
  Logged lg;

  CHECK(lg.plain(1, 2) == 103);

  // Cloned default argument.
  CHECK(lg.defaulted(2) == 42);
  CHECK(lg.defaulted(2, 3) == 5);

  // Braced-init-list argument binds the real parameter type.
  CHECK(lg.take({3, 4}) == 7);

  // Variadic member template.
  CHECK(lg.sum(1, 2, 3) == 6);
  CHECK(lg.sum() == 0);

  // Explicit template arguments work against the cloned heads.
  CHECK(lg.get<2>() == 20);
  CHECK(lg.scale<double, 3>(1.5) == 4.5);

  // Constraints cloned.
  CHECK(lg.only_int(21) == 21);
  CHECK(lg.twice(4) == 8);

  // Ref-qualifiers cloned; the rvalue-qualified clone moves the receiver.
  CHECK(lg.lref() == 1);
  CHECK(std::move(lg).rref() == 2);

  // Overload sets: each member cloned individually.
  CHECK(lg.pick(1) == 1);
  CHECK(lg.pick(1.5) == 2);

  CHECK(lg.calls == 14);
  return 1;
}
static_assert(use_all() == 1);

// The cloned constraints are *named* constraints, participating in overload
// viability the same way the originals do.
template <class T>
constexpr bool can_only_int = requires(Logged& lg, T v) { lg.only_int(v); };
static_assert(can_only_int<int>);
static_assert(!can_only_int<double>);

template <class T>
constexpr bool can_twice = requires(Logged& lg, T v) { lg.twice(v); };
static_assert(can_twice<long>);
static_assert(!can_twice<std::string>);

// The rvalue-qualified member is not callable on an lvalue (cloned
// ref-qualifier, not a deduced collapse).
template <class T>
constexpr bool can_rref_on_lvalue = requires(T& lg) { lg.rref(); };
static_assert(!can_rref_on_lvalue<Logged>);

// ----------------------------------------------------------------------------
// Renaming.
// ----------------------------------------------------------------------------
struct Renamed {
  Impl impl;
  mutable int calls = 0;

  consteval {
    auto d = declaration_of(^^Impl::plain, {.name = ^^{ renamed_plain }});
    auto call = forwarding_call_for(d, ^^{ impl });
    queue_injection(^^{
      \(d) {
        ++calls;
        return \(call);
      }
    });
  }
};

static_assert([] {
  Renamed r;
  CHECK(r.renamed_plain(1, 2) == 103);
  CHECK(r.calls == 1);
  return 1;
}());

// A renamed clone still forwards to the source member's name on the
// receiver: renaming controls what the wrapper exposes, not what it calls.
static_assert(std::meta::is_declaration_spec(std::meta::declaration_of(^^Impl::plain)));
static_assert(!std::meta::is_declaration_spec(^^Impl::plain));

// ----------------------------------------------------------------------------
// The real thing: the three vector member templates from the LoggingVector
// exercise, cloned with their actual heads.
// ----------------------------------------------------------------------------
consteval auto vec_wrappers(std::meta::info V,
                            std::initializer_list<std::string_view> names)
    -> std::meta::token_sequence {
  std::meta::token_sequence out = ^^{};
  for (std::meta::info m : members_of(V, std::meta::access_context::current())) {
    if (!has_identifier(m))
      continue;
    bool wanted = false;
    for (std::string_view n : names)
      wanted |= (identifier_of(m) == n);
    if (!wanted)
      continue;
    if (!is_function_template(m) &&
        (!is_function(m) || is_special_member_function(m) ||
         is_static_member(m)))
      continue;

    auto d = declaration_of(m);
    auto call = forwarding_call_for(d, ^^{ impl });
    out += ^^{
      \(d) {
        ++calls;
        return \(call);
      }
    };
  }
  return out;
}

struct LoggedVec {
  std::vector<int> impl;
  mutable int calls = 0;

  consteval {
    queue_injection(vec_wrappers(
        ^^std::vector<int>, {"emplace_back", "emplace", "push_back", "size"}));
  }
};

constexpr int use_vec() {
  LoggedVec lv;
  lv.push_back(1);
  int& r = lv.emplace_back(2);
  CHECK(r == 2);
  lv.emplace(lv.impl.begin(), 0);
  CHECK(lv.size() == 3);
  CHECK(lv.impl[0] == 0 && lv.impl[1] == 1 && lv.impl[2] == 2);
  CHECK(lv.calls == 4);
  return 1;
}
static_assert(use_vec() == 1);

// ----------------------------------------------------------------------------
// Uninstantiated defaults: a member template of a specialization keeps its
// default as the *pattern's* expression, which can reference the enclosing
// class's parameters; the clone must resolve those against the source
// specialization while keeping the member-parameter-dependent parts lazy.
// ----------------------------------------------------------------------------
namespace defaults {

template <class T>
struct Outer {
  template <class V>
  constexpr int f(int n = sizeof(T) + sizeof(V)) const {
    return n;
  }
  // A default that is invalid when instantiated; depends only on the
  // member's own parameter, so cloning must not instantiate it.
  template <class V>
  constexpr int g(int x, [[maybe_unused]] int bad = V::missing()) const {
    return x;
  }
};

struct W {
  Outer<long> impl;

  consteval {
    auto fd = std::meta::declaration_of(^^Outer<long>::template f);
    auto gd = std::meta::declaration_of(^^Outer<long>::template g);
    queue_injection(^^{
      \(fd) { return \(std::meta::forwarding_call_for(fd, ^^{ impl })); }
      \(gd) { return \(std::meta::forwarding_call_for(gd, ^^{ impl })); }
    });
  }
};

// T resolves against the source specialization (long), V stays the clone's.
static_assert(W{}.f<int>() == sizeof(long) + sizeof(int));
static_assert(W{}.f<char[3]>() == sizeof(long) + 3);
static_assert(W{}.f<int>(7) == 7);

// The bad default was cloned, not instantiated; supplying the argument
// never touches it.
static_assert(W{}.g<int>(11, 0) == 11);

}  // namespace defaults

// ----------------------------------------------------------------------------
// Array and function parameters adjust to pointers in the clone, exactly as
// in the source; references to arrays stay references.
// ----------------------------------------------------------------------------
namespace adjust {

struct U {
  constexpr int arr(int values[2]) const { return values ? values[0] : 7; }
  constexpr int fn(int callback()) const { return callback ? callback() : 7; }
  constexpr int ref(int (&values)[2]) const { return values[1]; }
};

struct W {
  U impl;
  consteval {
    for (std::string_view n : {"arr", "fn", "ref"})
      for (std::meta::info m :
           members_of(^^U, std::meta::access_context::current()))
        if (is_function(m) && has_identifier(m) && identifier_of(m) == n) {
          auto d = std::meta::declaration_of(m);
          auto call = std::meta::forwarding_call_for(d, ^^{ impl });
          queue_injection(^^{ \(d) { return \(call); } });
        }
  }
};

static_assert(W{}.arr(nullptr) == 7);
static_assert(W{}.fn(nullptr) == 7);
static_assert([] {
  int two[2] = {5, 6};
  int arg[2] = {8, 9};
  CHECK(W{}.arr(arg) == 8);
  CHECK(W{}.ref(two) == 6);
  return 1;
}());

}  // namespace adjust

// ----------------------------------------------------------------------------
// Receiver handling: grouping survives a '*ptr' receiver, a const clone
// dispatches to the const overload even through a mutable member, and an
// rvalue-qualified clone moves its receiver even when it is a dereference.
// ----------------------------------------------------------------------------
namespace receivers {

struct U {
  constexpr int f() { return 1; }
  constexpr int f() const { return 2; }
  constexpr int rv() && { return 3; }
};

consteval std::meta::info pick_f(bool want_const) {
  for (std::meta::info m :
       members_of(^^U, std::meta::access_context::current()))
    if (is_function(m) && has_identifier(m) && identifier_of(m) == "f" &&
        is_const(type_of(m)) == want_const)
      return m;
  return {};
}

struct ThroughPointer {
  U* ptr;
  consteval {
    auto d = std::meta::declaration_of(pick_f(false));
    queue_injection(^^{
      \(d) { return \(std::meta::forwarding_call_for(d, ^^{ *ptr })); }
    });
    auto rd = std::meta::declaration_of(^^U::rv);
    queue_injection(^^{
      \(rd) { return \(std::meta::forwarding_call_for(rd, ^^{ *ptr })); }
    });
  }
};

struct ThroughMutable {
  mutable U impl;
  consteval {
    // Only the const overload is cloned; it must call the const overload
    // even though 'impl' is mutable (and so non-const here).
    auto d = std::meta::declaration_of(pick_f(true));
    queue_injection(^^{
      \(d) { return \(std::meta::forwarding_call_for(d, ^^{ impl })); }
    });
  }
};

static_assert([] {
  U u;
  ThroughPointer tp{&u};
  CHECK(tp.f() == 1);
  CHECK(std::move(tp).rv() == 3);  // dereferenced receiver still moves
  CHECK(ThroughMutable{}.f() == 2);
  return 1;
}());

}  // namespace receivers

// ----------------------------------------------------------------------------
// argument_list_for: the fragment beneath forwarding_call_for, for calls
// whose callee the generator spells itself; {.forward} chooses between
// forwarding and passing lvalues.
// ----------------------------------------------------------------------------
namespace arglist {

struct Tracer {
  int copies = 0;
  int moves = 0;
  constexpr Tracer() = default;
  constexpr Tracer(const Tracer& o) : copies(o.copies + 1), moves(o.moves) {}
  constexpr Tracer(Tracer&& o) : copies(o.copies), moves(o.moves + 1) {}
};

struct U {
  constexpr int take(Tracer t) const { return t.copies * 10 + t.moves; }

  // A nonterminal template pack: forwarding_call_for must refuse this
  // (explicit template arguments would be a non-deduced context), but a
  // plain deduced call through argument_list_for forwards it fine.
  template <class... Ts, class V>
  constexpr int nonterminal(V v, Ts... ts) const {
    return v + (0 + ... + ts);
  }
};

struct W {
  U impl;

  consteval {
    // Forwarded: the by-value Tracer is moved into the inner call.
    auto fd = std::meta::declaration_of(^^U::take, {.name = ^^{ fwd }});
    queue_injection(^^{
      \(fd) { return \(std::meta::forwarding_call_for(fd, ^^{ impl })); }
    });

    // Lvalue mode: the same call copies, and the wrapper can still use the
    // argument afterwards.
    auto ld = std::meta::declaration_of(^^U::take, {.name = ^^{ lval }});
    queue_injection(^^{
      \(ld) {
        int r = (impl).take(\(std::meta::argument_list_for(
            ld, {.forward = false})));
        return r + p0.copies;  // still valid: p0 was not moved from
      }
    });

    // forwarding_call_for grew the same option.
    auto cd = std::meta::declaration_of(^^U::take, {.name = ^^{ lcall }});
    queue_injection(^^{
      \(cd) {
        return \(std::meta::forwarding_call_for(cd, ^^{ impl },
                                                 {.forward = false}));
      }
    });

    // The nonterminal-pack member, forwarded through the fragment with a
    // deduced inner call.
    auto nd = std::meta::declaration_of(^^U::template nonterminal);
    queue_injection(^^{
      \(nd) { return (impl).nonterminal(\(std::meta::argument_list_for(nd))); }
    });
  }
};

// A prvalue argument constructs p0 in place (guaranteed elision), so the
// only copy/move observed is the transfer from p0 into take's parameter.
static_assert(W{}.fwd(Tracer{}) == 1);    // forwarded: one move
static_assert(W{}.lval(Tracer{}) == 10);  // lvalue mode: one copy, and
                                          // p0 (never moved from) is still
                                          // usable after the call
static_assert(W{}.lcall(Tracer{}) == 10); // same, through forwarding_call_for
static_assert(W{}.nonterminal(1, 2, 3) == 6); // pack forwarded positionally

}  // namespace arglist

// ----------------------------------------------------------------------------
// Distinct descriptions mangle distinctly (usable as template arguments in
// code generation, not just constant evaluation).
// ----------------------------------------------------------------------------
namespace mangling {

struct U {
  int a();
  int b();
};

constexpr auto da = std::meta::declaration_of(^^U::a);
constexpr auto db = std::meta::declaration_of(^^U::b);
static_assert(da != db);

template <std::meta::info D>
int tag() {
  if constexpr (D == da)
    return 1;
  else
    return 2;
}

int use_mangling() { return tag<da>() + 10 * tag<db>(); }

}  // namespace mangling

int main() {
  if (use_all() != 1)
    return 1;
  if (use_vec() != 1)
    return 2;
  if (mangling::use_mangling() != 21)
    return 3;
  return 0;
}
