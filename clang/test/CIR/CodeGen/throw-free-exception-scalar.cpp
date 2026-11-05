// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fclangir -emit-cir -fexceptions -fcxx-exceptions %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

int may_throw();

int test_scalar_throw_cleanup() {
  try {
    throw may_throw();
  } catch (...) {
    return 1;
  }
}

// Verify we clean up the exception object if evaluating the scalar throw
// operand throws after __cxa_allocate_exception / cir.alloc.exception.
// CHECK-LABEL: cir.func {{.*}}@_Z{{[0-9]+}}test_scalar_throw_cleanupv
// CHECK:      %[[EXN:.*]] = cir.alloc.exception
// CHECK:      cir.call exception @_Z9may_throwv(){{.*}} cleanup {
// CHECK:        %[[EXN_VP:.*]] = cir.cast bitcast %[[EXN]] : !cir.ptr<!s32i> -> !cir.ptr<!void>
// CHECK-NEXT:   cir.call @__cxa_free_exception(%[[EXN_VP]]) nothrow
// CHECK:      }
// CHECK:      cir.throw %[[EXN]]
