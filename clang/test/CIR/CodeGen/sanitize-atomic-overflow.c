// RUN: %clang_cc1 -triple x86_64-apple-macosx10.14.0 -fsanitize=unsigned-integer-overflow \
// RUN:   -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-apple-macosx10.14.0 -fsanitize=unsigned-integer-overflow \
// RUN:   -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Verify that atomic compound assignment with -fsanitize=unsigned-integer-overflow
// uses CAS loop with overflow check instead of plain atomicrmw.

_Atomic(unsigned) atomic;

void cmpd_assign(void) {
  atomic += 1;
}

// Both should use cmpxchg loop with overflow check:
// CIR: define {{.*}} @cmpd_assign
// CIR: llvm.uadd.with.overflow.i32
// CIR: __ubsan_handle_add_overflow
// CIR: cmpxchg
// CIR-NOT: atomicrmw

// OGCG: define {{.*}} @cmpd_assign
// OGCG: llvm.uadd.with.overflow.i32
// OGCG: __ubsan_handle_add_overflow
// OGCG: cmpxchg
// OGCG-NOT: atomicrmw
