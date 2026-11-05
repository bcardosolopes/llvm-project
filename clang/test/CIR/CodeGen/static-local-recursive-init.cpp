// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR

// Verify CIR handles static local variables with non-constant initializers
// that result from recursive function calls.

// CIR: cir.global {{.*}} @_ZGVZ1avE1b = #cir.int<0> : !u64i
// CIR: cir.func {{.*}} @_Z1av
// CIR:   cir.call @__cxa_guard_acquire
// CIR:   cir.call @__cxa_guard_release

int a() { static int b = a(); }
