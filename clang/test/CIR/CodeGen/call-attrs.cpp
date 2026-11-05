// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s
//
// Regression test: function call attributes
// (nothrow, etc.) should be propagated to call operations, not discarded.

void nothrow_fn() noexcept;

void test() {
    nothrow_fn();
}

// The call to nothrow_fn should have the nothrow attribute.
// CHECK-LABEL: cir.func {{.*}}@_Z4testv()
// CHECK: cir.call @_Z10nothrow_fnv() nothrow
