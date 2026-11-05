// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck %s --input-file=%t.cir

// When a CK_NoOp cast adds volatile qualification, the load must go through
// the l-value to respect volatile semantics.

int test_volatile_noop_cast() {
    volatile int x = 42;
    // Casting volatile int to int via NoOp should emit a volatile load.
    return static_cast<int>(x);
}

// CHECK-LABEL: @_Z23test_volatile_noop_castv
// CHECK: cir.load volatile
