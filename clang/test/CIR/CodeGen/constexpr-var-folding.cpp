// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

constexpr int kConstexpr = 42;
const int kConst = 100;

// constexpr variable used as rvalue should fold to constant, not load from global.
int use_constexpr() {
  return kConstexpr;
}

// CHECK-LABEL: @_Z13use_constexprv
// CHECK:         cir.const #cir.int<42> : !s32i
// CHECK-NOT:     cir.get_global @kConstexpr
// CHECK:         cir.return

// const variable with constant initializer should also fold.
int use_const() {
  return kConst;
}

// CHECK-LABEL: @_Z9use_constv
// CHECK:         cir.const #cir.int<100> : !s32i
// CHECK-NOT:     cir.get_global @kConst
// CHECK:         cir.return
