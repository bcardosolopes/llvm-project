//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// UNSUPPORTED: c++03 || c++11 || c++14 || c++17 || c++20
// ADDITIONAL_COMPILE_FLAGS: -freflection

// Test splicing base specifier reflections

#include <meta>
#include <type_traits>
#include <utility>
#include <vector>
#include <cassert>

struct B {
  int x = 42;
};

struct D : B {
  int y = 100;
};

consteval auto get_base_specifier() {
  auto bases = bases_of(^^D, std::meta::access_context::unchecked());
  return bases[0];
}

// =============================================================================
// Part 1: decltype tests - verify expression type
// =============================================================================

constexpr auto base_spec = get_base_specifier();

// Test decltype with different cv-qualifications on lvalues
static_assert(std::is_same_v<decltype(std::declval<D&>().[:base_spec:]), B&>);
static_assert(std::is_same_v<decltype(std::declval<D const&>().[:base_spec:]), const B&>);
static_assert(std::is_same_v<decltype(std::declval<D volatile&>().[:base_spec:]), volatile B&>);
static_assert(std::is_same_v<decltype(std::declval<D const volatile&>().[:base_spec:]), const volatile B&>);

// Test decltype with rvalues
static_assert(std::is_same_v<decltype(std::declval<D>().[:base_spec:]), B&&>);
static_assert(std::is_same_v<decltype(std::declval<D&&>().[:base_spec:]), B&&>);
static_assert(std::is_same_v<decltype(std::declval<const D&&>().[:base_spec:]), const B&&>);

// =============================================================================
// Part 2: auto deduction tests
// =============================================================================

constexpr bool test_auto_deduction() {
    D d;
    const D& cd = d;
    constexpr auto r = get_base_specifier();

    // auto copies
    auto a1 = d.[:r:];
    static_assert(std::is_same_v<decltype(a1), B>);

    auto a2 = cd.[:r:];
    static_assert(std::is_same_v<decltype(a2), B>);

    // auto& deduces reference
    auto& ref1 = d.[:r:];
    static_assert(std::is_same_v<decltype(ref1), B&>);

    auto& ref2 = cd.[:r:];
    static_assert(std::is_same_v<decltype(ref2), const B&>);

    // const auto& deduces const reference
    const auto& cref1 = d.[:r:];
    static_assert(std::is_same_v<decltype(cref1), const B&>);

    const auto& cref2 = cd.[:r:];
    static_assert(std::is_same_v<decltype(cref2), const B&>);

    // auto&& deduces correctly (forwarding reference)
    auto&& fwd1 = d.[:r:];  // lvalue -> lvalue ref
    static_assert(std::is_same_v<decltype(fwd1), B&>);

    auto&& fwd2 = cd.[:r:];  // const lvalue -> const lvalue ref
    static_assert(std::is_same_v<decltype(fwd2), const B&>);

    auto&& fwd3 = D{}.[:r:];  // rvalue -> rvalue ref
    static_assert(std::is_same_v<decltype(fwd3), B&&>);

    return true;
}

static_assert(test_auto_deduction());

// =============================================================================
// Part 3: decltype(auto) tests
// =============================================================================

constexpr bool test_decltype_auto() {
    D d;
    const D& cd = d;
    constexpr auto r = get_base_specifier();

    // decltype(auto) should preserve expression category
    decltype(auto) da1 = d.[:r:];
    static_assert(std::is_same_v<decltype(da1), B&>);

    decltype(auto) da2 = cd.[:r:];
    static_assert(std::is_same_v<decltype(da2), const B&>);

    // Check that the reference actually refers to the subobject
    assert(&da1 == static_cast<B*>(&d));
    assert(&da2 == static_cast<const B*>(&cd));

    return true;
}

static_assert(test_decltype_auto());

// =============================================================================
// Part 4: Template argument deduction tests
// =============================================================================

template<class T>
constexpr bool check_deduced_base(T&&) {
    return std::is_same_v<std::remove_cvref_t<T>, B>;
}

template<class T>
constexpr bool check_is_lvalue_ref(T&&) {
    return std::is_lvalue_reference_v<T&&>;
}

template<class T>
constexpr bool check_is_const(T&&) {
    return std::is_const_v<std::remove_reference_t<T>>;
}

constexpr bool test_template_deduction() {
    D d;
    const D& cd = d;
    constexpr auto r = get_base_specifier();

    // Should deduce T = B for all these
    assert(check_deduced_base(d.[:r:]));
    assert(check_deduced_base(cd.[:r:]));
    assert(check_deduced_base(D{}.[:r:]));

    // Check value category is preserved
    assert(check_is_lvalue_ref(d.[:r:]) == true);   // lvalue
    assert(check_is_lvalue_ref(cd.[:r:]) == true);  // lvalue
    assert(check_is_lvalue_ref(D{}.[:r:]) == false); // rvalue

    // Check const is preserved
    assert(check_is_const(d.[:r:]) == false);
    assert(check_is_const(cd.[:r:]) == true);

    return true;
}

static_assert(test_template_deduction());

// =============================================================================
// Part 5: Variadic template deduction tests
// =============================================================================

template<class... Args>
constexpr bool all_are_base(Args&&...) {
    return (std::is_same_v<std::remove_cvref_t<Args>, B> && ...);
}

constexpr bool test_variadic_deduction() {
    D d;
    const D& cd = d;
    constexpr auto r = get_base_specifier();

    assert(all_are_base(d.[:r:]));
    assert(all_are_base(cd.[:r:]));
    assert(all_are_base(d.[:r:], cd.[:r:]));
    assert(all_are_base(d.[:r:], cd.[:r:], D{}.[:r:]));

    return true;
}

static_assert(test_variadic_deduction());

// =============================================================================
// Part 6: Reference binding tests
// =============================================================================

constexpr bool test_reference_binding() {
    D d;
    d.x = 123;
    const D& cd = d;
    constexpr auto r = get_base_specifier();

    // Store in reference
    const B& b = cd.[:r:];
    assert(b.x == 123);
    assert(&b == static_cast<const B*>(&cd));

    // Mutable reference
    B& mb = d.[:r:];
    mb.x = 456;
    assert(d.x == 456);

    return true;
}

static_assert(test_reference_binding());

// =============================================================================
// Part 7: Function return type tests
// =============================================================================

constexpr B get_base_copy(D& d) {
    constexpr auto r = get_base_specifier();
    return d.[:r:];
}

constexpr const B& get_base_cref(const D& d) {
    constexpr auto r = get_base_specifier();
    return d.[:r:];
}

constexpr B& get_base_ref(D& d) {
    constexpr auto r = get_base_specifier();
    return d.[:r:];
}

constexpr bool test_return_types() {
    D d;
    d.x = 789;

    B bcopy = get_base_copy(d);
    assert(bcopy.x == 789);

    const B& bcref = get_base_cref(d);
    assert(bcref.x == 789);
    assert(&bcref == static_cast<const B*>(&d));

    B& bref = get_base_ref(d);
    assert(bref.x == 789);
    bref.x = 999;
    assert(d.x == 999);

    return true;
}

static_assert(test_return_types());

// =============================================================================
// Part 8: Constrained template + explicit specialization with auto& parameter
// This tests the specific bug where DerivedToBase casts were stripped during
// template instantiation in TransformInitializer.
// =============================================================================

// A simple output iterator for testing
struct TestOutputIterator {
    using difference_type = std::ptrdiff_t;
    using value_type = char;
    constexpr TestOutputIterator& operator*() { return *this; }
    constexpr TestOutputIterator& operator++() { return *this; }
    constexpr TestOutputIterator operator++(int) { return *this; }
    constexpr TestOutputIterator& operator=(char) { return *this; }
};
static_assert(std::output_iterator<TestOutputIterator, char>);

template <class T>
struct Container {
    constexpr auto begin() { return TestOutputIterator{}; }
};

// Constrained variadic template - the constraint is key to triggering the bug
template <std::output_iterator<char> OutIt, class... Args>
constexpr bool constrained_all_are_base(OutIt, Args&&...) {
    return (std::is_same_v<std::remove_cvref_t<Args>, B> && ...);
}

// Primary template declaration
template <class T>
struct ExplicitSpecWrapper;

// Explicit specialization - auto& ctx parameter is key to triggering the bug
template <>
struct ExplicitSpecWrapper<D> {
    template<typename Ctx>
    static constexpr bool test(D const& object, Ctx& ctx) {
        constexpr auto bs = get_base_specifier();

        // This was the failing case: ctx.out() + constrained template + splice
        return constrained_all_are_base(ctx.begin(), object.[:bs:]);
    }
};

constexpr bool test_explicit_spec_constrained() {
    D d;
    Container<char> ctx;

    // This should deduce Args = {const B&}, NOT {const D&}
    bool result = ExplicitSpecWrapper<D>::test(d, ctx);
    assert(result);

    return true;
}

static_assert(test_explicit_spec_constrained());

// =============================================================================
// Original tests below
// =============================================================================

constexpr int test_direct_base() {
  D d;
  constexpr auto r = get_base_specifier();
  // Access the base subobject via splice
  return d.[:r:].x;
}

static_assert(test_direct_base() == 42);

// Test with multiple inheritance
struct A { int a = 1; };
struct B2 { int b = 2; };
struct C : A, B2 { int c = 3; };

consteval auto get_second_base() {
  auto bases = bases_of(^^C, std::meta::access_context::unchecked());
  return bases[1];  // B2
}

constexpr int test_second_base() {
  C c;
  constexpr auto r = get_second_base();
  return c.[:r:].b;
}

static_assert(test_second_base() == 2);

// Test with arrow operator
constexpr int test_arrow() {
  D d;
  D* pd = &d;
  constexpr auto r = get_base_specifier();
  return pd->[:r:].x;
}

static_assert(test_arrow() == 42);

// Test with deeper inheritance
struct E : D {
  int z = 200;
};

consteval auto get_d_as_base_of_e() {
  auto bases = bases_of(^^E, std::meta::access_context::unchecked());
  return bases[0];  // D (the direct base of E)
}

constexpr int test_deeper_inheritance() {
  E e;
  e.y = 123;
  constexpr auto r = get_d_as_base_of_e();
  return e.[:r:].y;
}

static_assert(test_deeper_inheritance() == 123);

// Test that we can chain accesses through the base
constexpr int test_chain_access() {
  E e;
  constexpr auto r = get_d_as_base_of_e();
  // e.[:r:] gives the D subobject, then .x accesses B::x through D
  return e.[:r:].x;
}

static_assert(test_chain_access() == 42);

// Test using a base specifier from an intermediate base when a direct
// conversion to the base type would be ambiguous.
struct OtherB : B {
  int other = 300;
};

struct AmbiguousB : D, OtherB {
  int w = 400;
};

constexpr bool test_more_derived_ambiguous_target() {
  AmbiguousB object;
  static_cast<D&>(object).x = 1234;
  static_cast<OtherB&>(object).x = 5678;

  constexpr auto r = get_base_specifier(); // B as a base of D
  B& b = object.[:r:];
  assert(&b == static_cast<B*>(static_cast<D*>(&object)));
  assert(b.x == 1234);
  return true;
}

static_assert(test_more_derived_ambiguous_target());

// Test subobjects_of - returns bases then nonstatic data members
struct F {
  int f1 = 10;
  int f2 = 20;
};

struct G : F {
  int g1 = 30;
  int g2 = 40;
};

consteval auto test_subobjects_of() {
  constexpr auto ctx = std::meta::access_context::unchecked();
  auto subs = subobjects_of(^^G, ctx);

  // Should have 3 subobjects: F (base), g1, g2
  if (subs.size() != 3) return false;

  // First is the base class specifier
  if (!is_base(subs[0])) return false;

  // Next are the data members
  if (!is_nonstatic_data_member(subs[1])) return false;
  if (!is_nonstatic_data_member(subs[2])) return false;

  if (identifier_of(subs[1]) != "g1") return false;
  if (identifier_of(subs[2]) != "g2") return false;

  return true;
}

static_assert(test_subobjects_of());

// Test that subobjects_of works with splicing
consteval auto get_subobject(int idx) {
  return subobjects_of(^^G, std::meta::access_context::unchecked())[idx];
}

constexpr int test_subobjects_splice() {
  G g;

  // Access the base via splice
  constexpr auto base = get_subobject(0);
  int base_sum = g.[:base:].f1 + g.[:base:].f2;

  // Access the data members via splice
  constexpr auto m1 = get_subobject(1);
  constexpr auto m2 = get_subobject(2);
  int member_sum = g.[:m1:] + g.[:m2:];

  return base_sum + member_sum;
}

static_assert(test_subobjects_splice() == 10 + 20 + 30 + 40);

int main() {}
