// RUN: %clang_cc1 -std=c++2d -triple x86_64-unknown-linux-gnu -O0 -emit-llvm %s -o - | FileCheck %s

extern int a, b;

// A prvalue do-expression that is the whole initializer writes its result
// straight into the variable: no `doexpr.result` temporary, and no store/load
// pair copying out of one on the join path.

// CHECK-LABEL: define {{.*}} @_Z6direct
// CHECK-NOT:     doexpr.result
// CHECK:         alloca i32
// CHECK-NOT:     doexpr.result
// CHECK:       ret
int direct() {
  int x = do {
    if (a > b) do_return a;
    do_return b;
  };
  return x;
}

// A conversion between the do-expression and the variable means the result is
// not the variable's value, so the temporary stays.

// CHECK-LABEL: define {{.*}} @_Z9converted
// CHECK:         %doexpr.result = alloca i32
long converted() {
  long x = do -> int { do_return a; };
  return x;
}

// A volatile destination: the number and kind of writes is observable, so the
// temporary stays and the single volatile store is preserved.

// CHECK-LABEL: define {{.*}} @_Z12volatile_dst
// CHECK:         %doexpr.result = alloca i32
// CHECK:         store volatile i32
int volatile_dst() {
  volatile int x = do {
    if (a > b) do_return a;
    do_return b;
  };
  return x;
}

// An atomic destination, likewise.

// CHECK-LABEL: define {{.*}} @_Z10atomic_dst
// CHECK:         %doexpr.result = alloca i32
int atomic_dst() {
  _Atomic int x = do { do_return a; };
  return x;
}

// Not an initializer at all: nothing to write into, so the temporary stays.

// CHECK-LABEL: define {{.*}} @_Z10not_a_init
// CHECK:         %doexpr.result = alloca i32
int not_a_init() {
  return (do {
    if (a > b) do_return a;
    do_return b;
  }) + 1;
}

// Nested do-expressions, each the whole initializer of its own variable:
// neither needs a temporary.

// CHECK-LABEL: define {{.*}} @_Z6nested
// CHECK-NOT:     doexpr.result
// CHECK:       ret
int nested() {
  int outer = do {
    int inner = do { do_return a; };
    do_return inner + 1;
  };
  return outer;
}
