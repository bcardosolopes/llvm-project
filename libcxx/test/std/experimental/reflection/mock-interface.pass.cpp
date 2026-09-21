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

// mock<Interface>: the MOCK_METHOD killer.
//
// GMock requires a hand-written subclass repeating every member:
//
//   struct MockCalculator : Calculator {
//     MOCK_METHOD(int, add, (int, int), (override));
//     MOCK_METHOD(int, negate, (int), (const, override));
//     MOCK_METHOD(std::string, describe, (), (const, override));
//     ...                          // and it breaks on default arguments
//   };
//
// Here mock<I> derives from I and, per virtual member, injects (a) a
// std::function handler member named after it and (b) a *clone* of the
// member (std::meta::declaration_of) whose body records the call and
// dispatches to the handler, or default-constructs the result. No macros,
// no repetition; new interface members are mocked by recompiling.
//
// The clone needs no 'virtual': a member with the same name, signature, and
// cv/ref-qualifiers as an inherited virtual *implicitly* overrides it. The
// generator still applies two *declaration transformations* to the
// description before injecting:
//
//   auto d = declaration_of(m);
//   d = make_override(d);                        // inject '... override'
//   if (is_noexcept(type_of(m)))
//     d = make_noexcept(d);                      // mirror a noexcept virtual
//
// make_override turns silent signature drift (interface changed, clone now
// overrides nothing) into the same error a written 'override' gives. And
// since declaration_of deliberately does not clone exception
// specifications, make_noexcept is how the mock mirrors a noexcept virtual
// -- without it, the laxer override would be ill-formed. That the pure
// virtuals are all overridden is observable: mock<I> is not abstract.
//
// (Overloaded virtuals would need disambiguated handler names.)
//
// -----------------------------------------------------------------------
// Where this sits relative to real mocking frameworks
// -----------------------------------------------------------------------
//
// There are two schools of mocking API:
//
// GMock is *expectation-first*: the test declares, BEFORE running the code
// under test, which calls must happen, with what arguments, how many times,
// and what they should do; verification is automatic when the mock is
// destroyed:
//
//   MockCalculator m;
//   EXPECT_CALL(m, add(2, 3))         // matcher: called with exactly (2, 3)
//       .Times(2)                     // cardinality
//       .WillOnce(Return(5))          // action for the 1st matching call
//       .WillRepeatedly(Return(6));   // action for the rest
//   EXPECT_CALL(m, negate(Gt(0)))     // matchers can be predicates
//       .WillOnce(Return(-1));
//   run_code_under_test(m);
//   // ~MockCalculator fails the test if add wasn't called exactly twice;
//   // a call matching no expectation already failed at the call site.
//
// Its weaker sibling ON_CALL(m, add(_, _)).WillByDefault(...) sets behavior
// with no requirement that the call happens. Note EXPECT_CALL *must* be a
// macro: 'add(2, 3)' inside it is not a call -- the macro tears the
// expression apart textually to recover a method name and a matcher list.
//
// Mockito/jest are *verify-after*: stub behavior up front, run the code,
// then assert on what was recorded.
//
// What this test implements is the second school: 'm.add_ = lambda' is
// ON_CALL's WillByDefault, and 'm.call_count("add")' is Mockito's verify().
//
// EXPECT_CALL parity would be pure library work on top of the same
// injection -- no further compiler support. Instead of a bare
// std::function, inject a richer slot per method
// ('method_slot<int(int, int)> add_;') holding a list of expectations
// (matcher + cardinality + action) and a default:
//
//   m.add_.expect(2, 3).times(2)      // EXPECT_CALL(m, add(2,3)).Times(2)
//         .will_once(5)               //   .WillOnce(Return(5))
//         .will_repeatedly(6);        //   .WillRepeatedly(Return(6));
//   m.negate_.expect(gt(0)).returns(-1);
//   run_code_under_test(m);
//   // ~mock verifies, reporting method names and observed calls.
//
// Because expect(...) is a real variadic member of a slot typed with the
// method's actual signature, matchers type-check against the true parameter
// types at compile time -- the reflection already delivered the signature,
// so the macro's reason for existing (re-parsing the call expression) is
// gone. Argument recording for verify-after style is the same trick:
// inject 'std::vector<std::tuple<Params...>> add_args_;' and push in the
// body.

#include <meta>
#include <algorithm>
#include <cassert>
#include <functional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

template <class I>
struct mock : I {
  mutable std::vector<std::string_view> calls_;

  int call_count(std::string_view name) const {
    return std::ranges::count(calls_, name);
  }

  consteval {
    for (std::meta::info m :
         members_of(^^I, std::meta::access_context::current())) {
      if (!is_function(m) || !is_virtual(m) || is_destructor(m) ||
          is_operator_function(m) || is_conversion_function(m))
        continue;

      auto name = identifier_of(m);
      auto d = std::meta::make_override(std::meta::declaration_of(m));
      if (is_noexcept(type_of(m)))
        d = std::meta::make_noexcept(d);
      std::meta::info ret = return_type_of(m);

      // The handler member's type: std::function<R(Params...)>.
      auto ptypes = std::meta::list_builder(^^{ , });
      for (std::meta::info p : parameters_of(m))
        ptypes += ^^{ \(type_of(p)) };

      // Forward the clone's parameters into the handler.
      auto args = std::meta::argument_list_for(d);

      auto handler = std::meta::id(name, "_");
      queue_injection(^^{
      public:
        std::function<\(ret)(\(ptypes))> \(handler);
      });

      queue_injection(^^{
      public:
        \(d) {
          calls_.push_back(\(std::meta::str_lit(name)));
          if (\(handler))
            return \(handler)(\(args));
          return \(ret)();
        }
      });
    }
  }
};

// ----------------------------------------------------------------------------
// The interface under test.
// ----------------------------------------------------------------------------
struct Calculator {
  virtual ~Calculator() = default;
  virtual int add(int a, int b) = 0;
  virtual int negate(int x) const = 0;         // const virtual
  virtual std::string describe() const = 0;    // class-type result
  virtual void reset() = 0;                    // void
  virtual int increment(int x, int by = 1) = 0;  // default argument
  virtual int fast(int x) const noexcept = 0;    // noexcept virtual
};

// Every pure virtual is (implicitly) overridden by its clone.
static_assert(std::is_abstract_v<Calculator>);
static_assert(!std::is_abstract_v<mock<Calculator>>);

// The handler members have the real signatures.
static_assert(std::is_same_v<decltype(mock<Calculator>::add_),
                             std::function<int(int, int)>>);
static_assert(std::is_same_v<decltype(mock<Calculator>::describe_),
                             std::function<std::string()>>);

// make_noexcept mirrored the noexcept virtual (a laxer override would not
// even have compiled).
static_assert(noexcept(std::declval<const mock<Calculator>&>().fast(1)));
static_assert(!noexcept(std::declval<mock<Calculator>&>().add(1, 2)));

// Transformations produce new, distinct descriptions.
static_assert(std::meta::declaration_of(^^Calculator::add) !=
              std::meta::make_override(std::meta::declaration_of(^^Calculator::add)));

// Code under test: only sees the interface.
int run_twice(Calculator& c, int start) {
  c.reset();
  return c.increment(c.increment(start));  // uses the default 'by'
}

int main(int, char**) {
  mock<Calculator> m;
  m.add_ = [](int a, int b) { return a + b; };
  m.increment_ = [](int x, int by) { return x + by; };

  Calculator& c = m;

  // Virtual dispatch lands in the mock.
  assert(c.add(2, 3) == 5);

  // Uninteresting calls (no handler) return a default-constructed result.
  assert(c.negate(5) == 0);
  assert(c.describe() == "");

  // Handlers can be (re)set mid-test.
  m.negate_ = [](int x) { return -x; };
  assert(c.negate(7) == -7);

  // The cloned default argument works through both static types. (GMock's
  // MOCK_METHOD cannot mock a defaulted parameter at all.)
  assert(c.increment(41) == 42);
  assert(m.increment(10, 5) == 15);

  // Drive it through interface-only code.
  assert(run_twice(c, 0) == 2);

  // The noexcept member mocks like any other.
  m.fast_ = [](int x) { return x * 3; };
  assert(c.fast(5) == 15);

  // Expectations.
  assert(m.call_count("add") == 1);
  assert(m.call_count("negate") == 2);
  assert(m.call_count("describe") == 1);
  assert(m.call_count("reset") == 1);
  assert(m.call_count("increment") == 4);
  assert(m.calls_.front() == "add");

  return 0;
}
