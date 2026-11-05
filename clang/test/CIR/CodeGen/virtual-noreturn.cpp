// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fclangir -emit-mlir=cir-abi %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

// Test that [[noreturn]] is not applied to virtual call sites, since
// overrides may not be noreturn.

struct A {
  [[noreturn]] virtual void f();
};

// Non-virtual noreturn for comparison.
[[noreturn]] void die();

void test_virtual(A *p) {
  // Virtual call should NOT have noreturn.
  // CHECK-LABEL: @_Z12test_virtualP1A
  // CHECK: cir.call %{{.+}}(%{{.+}}) {cir.call_arg_attrs = [{cir.align = 8 : i64, cir.dereferenceable = 8 : i64, cir.nonnull, cir.noundef}]} : (!cir.ptr<!cir.func<(!cir.ptr<!rec_A>)>>, !cir.ptr<!rec_A>) -> () loc
  p->f();
}

void test_direct() {
  // Non-virtual call SHOULD have noreturn.
  // CHECK-LABEL: @_Z11test_directv
  // CHECK: cir.call @_Z3diev() : () -> () extra(#{{.+}})
  die();
}
