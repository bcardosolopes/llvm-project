// RUN: %clang_cc1 -triple x86_64-unknown-unknown -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-unknown -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-unknown -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// Verify per-variable code_model attribute is preserved through CIR lowering.

// CIR: cir.global "private"  internal dso_local  @_ZL2v1 = #cir.int<0> : !s32i {alignment = 4 : i64, cir.code_model = 1 : i32}
static int v1 __attribute__((model("small")));

void use1() {
  v1 = 1;
}

// CIR: cir.global  external  @v2 = #cir.fp<0.000000e+00> : !cir.float {alignment = 4 : i64, cir.code_model = 4 : i32}
float v2 __attribute__((model("large")));

// LLVM: @_ZL2v1 = internal global i32 0, code_model "small", align 4
// LLVM: @v2 = global float 0.000000e+00, code_model "large", align 4
// OGCG: @_ZL2v1 = internal global i32 0, code_model "small", align 4
// OGCG: @v2 = global float 0.000000e+00, code_model "large", align 4
