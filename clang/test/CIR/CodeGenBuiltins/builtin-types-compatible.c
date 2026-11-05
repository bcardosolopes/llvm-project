// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s -check-prefix=OGCG

// __builtin_types_compatible_p has 'int' return type in C, not 'bool'.
// CIR must emit an integer constant, not a boolean constant.

// CIR-LABEL: @test_compatible
// CIR: cir.const #cir.int<1> : !s32i
// LLVM-LABEL: @test_compatible
// LLVM: ret i32 1
// OGCG-LABEL: @test_compatible
// OGCG: ret i32 1
int test_compatible(void) {
  return __builtin_types_compatible_p(int, signed int);
}

// CIR-LABEL: @test_incompatible
// CIR: cir.const #cir.int<0> : !s32i
// LLVM-LABEL: @test_incompatible
// LLVM: ret i32 0
// OGCG-LABEL: @test_incompatible
// OGCG: ret i32 0
int test_incompatible(void) {
  return __builtin_types_compatible_p(int, unsigned int);
}

// CIR-LABEL: @test_arithmetic
// CIR: cir.const #cir.int<1> : !s32i
// CIR: cir.const #cir.int<0> : !s32i
// LLVM-LABEL: @test_arithmetic
// OGCG-LABEL: @test_arithmetic
int test_arithmetic(void) {
  return __builtin_types_compatible_p(int, signed int) +
         __builtin_types_compatible_p(int, unsigned int);
}
