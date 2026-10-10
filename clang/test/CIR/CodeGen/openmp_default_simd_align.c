// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -Wno-unused-value -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -Wno-unused-value -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -Wno-unused-value -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=OGCG

enum e0 { E0 };
struct s0 {
  enum e0         a:31;
};

int f0(void) {
  return __builtin_omp_required_simd_align(struct s0);
}

// CIR: %[[CONST_16:.*]] = cir.const #cir.int<16> : !s32i
// CIR: cir.return %[[CONST_16]] : !s32i

// LLVM: ret i32 16

// OGCG: ret i32 16
