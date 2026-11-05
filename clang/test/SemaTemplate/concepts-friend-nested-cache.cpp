// RUN: %clang_cc1 -std=c++20 -verify %s
// expected-no-diagnostics

// A hidden friend defined in a nested class template, whose associated
// constraint refers to the *enclosing* template parameter (here through the
// injected type `type`), must be satisfied/failed per enclosing specialization.
//
// Regression: the unsubstituted-constraint-satisfaction cache key is built from
// the constraint's parameter mapping, but HashParameterMapping::TraverseTagType
// skipped nested TagTypes, so the enclosing template arguments were never
// hashed. Friends coming from two different specializations (e.g. Impl<1>::type
// and Impl<2>::type) then profiled identically and collided in the cache, so
// the second reused the first's satisfaction result. Depending on instantiation
// order this produced a bogus "no viable operator" or "ambiguous operator"
// error when composing two different specializations via ADL.

namespace nested_friend_constraint_cache {
template <class A, class B> inline constexpr bool is_same_v = false;
template <class A> inline constexpr bool is_same_v<A, A> = true;
template <class T> struct rcv { using type = T; };
template <class T> struct rcv<T &> { using type = T; };
template <class T> struct rcv<T &&> { using type = T; };
template <class T> using rcv_t = typename rcv<T>::type;
template <class A, class B> concept same_as = is_same_v<A, B>;

struct result_base {};

template <int N> struct Impl { struct type; };
template <int N> struct Impl<N>::type : result_base {
  // sender | adaptor: left operand is not a result
  template <class Target, class Self>
    requires(!__is_base_of(result_base, rcv_t<Target>)) &&
            same_as<rcv_t<Self>, type>
  friend int operator|(Target &&, Self &&) {
    return 1;
  }
  // adaptor | adaptor: left operand is also a result
  template <class Other, class Self>
    requires(__is_base_of(result_base, rcv_t<Other>)) &&
            same_as<rcv_t<Self>, type>
  friend int operator|(Other &&, Self &&) {
    return 2;
  }
};
template <int N> using R = typename Impl<N>::type;

// Impl<1> instantiated first (previously: "no viable operator").
int compose_1_then_2() {
  R<1> a;
  R<2> b;
  return a | b;
}

// Impl<2> instantiated first (previously: "ambiguous operator").
int compose_2_then_1() {
  R<2> b;
  R<1> a;
  return a | b;
}
} // namespace nested_friend_constraint_cache
