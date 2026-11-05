// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -fno-clangir-call-conv-lowering -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir --check-prefix=CIR %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -fno-clangir-call-conv-lowering -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s

// Test that __attribute__((used)) causes globals/functions to appear in
// @llvm.compiler.used (on ELF) or @llvm.used (on non-ELF).

__attribute__((used)) static int used_var = 99;
__attribute__((used)) static void used_func() {}
int other_var = 42;

// CIR: cir.llvm_compiler_used = [@_ZL8used_var, @_ZL9used_funcv]

// LLVM: @llvm.compiler.used = appending global [2 x ptr] [ptr @_ZL8used_var, ptr @_ZL9used_funcv], section "llvm.metadata"
