// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR %s --input-file=%t.cir
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM %s --input-file=%t.ll

// Verify that cir.bool values are handled correctly in comparisons and
// CIR-to-LLVM lowering. The codegen promotes bools to i32 before comparing,
// but the CmpOp lowering must also handle bool operands directly for
// robustness (e.g., from CIR-to-CIR passes).

bool cmp_bool_eq(bool a, bool b) { return a == b; }
// CIR: cir.func {{.*}} @_Z11cmp_bool_eqbb
// CIR:   %[[A:.*]] = cir.cast bool_to_int %{{.*}} : !cir.bool -> !s32i
// CIR:   %[[B:.*]] = cir.cast bool_to_int %{{.*}} : !cir.bool -> !s32i
// CIR:   cir.cmp(eq, %[[A]], %[[B]]) : !s32i, !cir.bool
// LLVM: define {{.*}} @_Z11cmp_bool_eqbb
// LLVM:   icmp eq i32 %{{.*}}, %{{.*}}

bool cmp_bool_ne(bool a, bool b) { return a != b; }
// CIR: cir.func {{.*}} @_Z11cmp_bool_nebb
// CIR:   cir.cmp(ne, %{{.*}}, %{{.*}}) : !s32i, !cir.bool
// LLVM: define {{.*}} @_Z11cmp_bool_nebb
// LLVM:   icmp ne i32 %{{.*}}, %{{.*}}
