// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -clangir-disable-passes %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

// Verify that variables with non-trivial destructors declared inside switch
// case bodies get conditional cleanup guards. In CIR's structured scope model,
// scope-exit cleanup code is shared by all paths exiting the scope. Without a
// conditional guard, a case that breaks before the declaration would still run
// the destructor on unconstructed memory.

struct Pool {
  int val;
  Pool() : val(42) {}
  ~Pool() {}
};

// CHECK-LABEL: @_Z4testi
int test(int token) {
  // CHECK: cir.alloca !cir.bool, !cir.ptr<!cir.bool>, ["cleanup.cond"]
  while (true) {
    switch (token) {
    case 0:
      break;
    default:
      // CHECK: cir.call @_ZN4PoolC1Ev
      // CHECK: cir.store {{.*}} !cir.bool
      Pool p;
      (void)p;
      // CHECK: cir.if
      // CHECK:   cir.call @_ZN4PoolD1Ev
      continue;
    }
    break;
  }
  // After the switch, the scope-exit destructor is guarded by the flag:
  // CHECK: cir.if
  // CHECK:   cir.call @_ZN4PoolD1Ev
  return 0;
}

// A switch nested in a loop is entered once per iteration, but the guard alloca
// lives in the function entry block, which runs once. The false initialization
// therefore has to be emitted on entry to the switch scope: otherwise the flag
// stays true after the first iteration that constructed the variable, and every
// later iteration taking a case that never constructed it destroys the slot
// anyway.
// CHECK-LABEL: @_Z15test_loop_resetPKhi
void test_loop_reset(const unsigned char *kinds, int n) {
  // CHECK: cir.alloca !cir.bool, !cir.ptr<!cir.bool>, ["cleanup.cond"]
  for (int i = 0; i < n; ++i) {
    // The reset is inside the loop body, ahead of the switch:
    // CHECK: cir.for
    // CHECK: cir.const #false
    // CHECK-NEXT: cir.store{{.*}}!cir.bool
    // CHECK: cir.switch
    switch (kinds[i]) {
    case 0:
      break;
    default:
      Pool p;
      (void)p;
    }
  }
}

// Verify no conditional guard when the variable is NOT inside a switch.
// CHECK-LABEL: @_Z12test_no_flagv
int test_no_flag() {
  // CHECK-NOT: cleanup.cond
  Pool p;
  (void)p;
  // CHECK: cir.call @_ZN4PoolD1Ev
  return 0;
}

// Verify that break from a case with a constructed variable clears the
// active flag after the inline cleanup, preventing double destruction
// from the scope-exit cleanup.
// CHECK-LABEL: @_Z10test_breaki
int test_break(int token) {
  // CHECK: cir.alloca !cir.bool, !cir.ptr<!cir.bool>, ["cleanup.cond"]
  switch (token) {
  case 0:
    break;
  default:
    // CHECK: cir.call @_ZN4PoolC1Ev
    // CHECK: cir.const #true
    // CHECK: cir.store
    Pool p;
    (void)p;
    // Break emits inline cleanup guarded by flag, then clears the flag:
    // CHECK: cir.if
    // CHECK:   cir.call @_ZN4PoolD1Ev
    // CHECK: cir.const #false
    // CHECK: cir.store
    // CHECK: cir.break
    break;
  }
  // Scope-exit cleanup is still guarded by the flag (now false on break path):
  // CHECK: cir.if
  // CHECK:   cir.call @_ZN4PoolD1Ev
  return 0;
}
