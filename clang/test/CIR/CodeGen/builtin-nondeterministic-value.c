// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=OGCG

// Test __builtin_nondeterministic_value.

float test_nondeterministic_float(float x) {
  return __builtin_nondeterministic_value(x);
}

// CIR-LABEL: @test_nondeterministic_float
// CIR: %[[POISON:.*]] = cir.const #cir.poison : !cir.float
// CIR: %[[FROZEN:.*]] = cir.freeze %[[POISON]] : !cir.float

// LLVM-LABEL: @test_nondeterministic_float
// LLVM: freeze float poison

// OGCG-LABEL: @test_nondeterministic_float
// OGCG: freeze float poison

int test_nondeterministic_int(int x) {
  return __builtin_nondeterministic_value(x);
}

// CIR-LABEL: @test_nondeterministic_int
// CIR: %[[POISON:.*]] = cir.const #cir.poison : !s32i
// CIR: %[[FROZEN:.*]] = cir.freeze %[[POISON]] : !s32i

// LLVM-LABEL: @test_nondeterministic_int
// LLVM: freeze i32 poison

// OGCG-LABEL: @test_nondeterministic_int
// OGCG: freeze i32 poison
