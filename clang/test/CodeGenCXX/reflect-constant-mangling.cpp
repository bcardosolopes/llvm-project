// RUN: %clang_cc1 -std=c++2d -freflection -triple x86_64-unknown-linux-gnu -emit-llvm -o - %s | FileCheck %s

// P4340 ext: a template argument normalized through a reflect_constant
// customization point mangles as a reference to the returned entity
// (L <mangled-name> E), so value-equal spellings in different TUs agree on
// the specialization and distinct normalized objects get distinct symbols.

namespace std::meta {
  using info = decltype(^^::);
}

consteval int gcd(int a, int b) {
  while (b) { int t = a % b; a = b; b = t; }
  return a;
}

struct Frac {
  int numer;
  int denom;
  consteval auto reflect_constant() const -> std::meta::info;
};

template <int N, int D> constexpr Frac interned = Frac{N, D};

consteval auto Frac::reflect_constant() const -> std::meta::info {
  int g = gcd(numer, denom);
  int n = numer / g, d = denom / g;
  if (n == 1 && d == 2) return ^^interned<1, 2>;
  if (n == 3 && d == 4) return ^^interned<3, 4>;
  return ^^interned<0, 1>;
}

template <Frac F> int f() { return F.numer; }

int use1() { return f<Frac{2, 4}>(); }  // normalizes to interned<1,2>
int use2() { return f<Frac{1, 2}>(); }  // same specialization as use1
int use3() { return f<Frac{3, 4}>(); }  // distinct specialization

// The two value-equal spellings share one definition, mangled by reference
// to the interned entity; the distinct value gets its own.
// CHECK: define linkonce_odr {{.*}} @_Z1fIL_Z8internedILi1ELi2EEEEiv()
// CHECK: define linkonce_odr {{.*}} @_Z1fIL_Z8internedILi3ELi4EEEEiv()
// CHECK-NOT: @_Z1fIL_Z8internedILi2ELi4EEEEiv
