// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir \
// RUN:   -emit-llvm -std=c++11 -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp \
// RUN:   -emit-llvm -std=c++11 -o - %s | FileCheck --check-prefix=OGCG %s

// Verify that an array section reduction where the innermost dimension is a
// plain subscript (not a section) does not crash. The reduction private
// variable is a scalar, not an array, so getOpenMPArrayElementCount must not
// be called on it.

struct S {
  int f;
  S() : f(0) {}
  S(int a) : f(a) {}
  S &operator&(const S &) { return *this; }
  ~S() {}
};

S **foo();

void test() {
  S **var2 = foo();
  // var2[1:1][1] is an ArraySectionExpr but the private type is scalar S.
#pragma omp parallel
#pragma omp for reduction(& : var2[1 : 1][1])
  for (int i = 0; i < 10; ++i)
    ;
}

// Just verify it compiles without crashing and produces a function.
// CHECK-LABEL: define {{.*}} @_Z4testv
// OGCG-LABEL: define {{.*}} @_Z4testv
