// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s -check-prefix=OGCG

struct Agg { int a; long b; };
consteval Agg retAgg() { return {13, 17}; }

long test_retAgg() {
  long b = retAgg().b;
  return b;
}

// CIR-LABEL: @_Z11test_retAggv
// CIR:   %[[CONST:.*]] = cir.const #cir.const_record<{#cir.int<13> : !s32i, #cir.int<17> : !s64i}> : !rec_Agg
// CIR:   cir.store align(8) %[[CONST]], %{{.*}} : !rec_Agg, !cir.ptr<!rec_Agg>

// LLVM-LABEL: @_Z11test_retAggv
// LLVM:   store %struct.Agg { i32 13, i64 17 }, ptr %{{.*}}, align 8

// OGCG-LABEL: @_Z11test_retAggv
// OGCG:   store i32 13, ptr %{{.*}}, align 8
// OGCG:   store i64 17, ptr %{{.*}}, align 8

int test_retAgg_first() {
  int a = retAgg().a;
  return a;
}

// CIR-LABEL: @_Z17test_retAgg_firstv
// CIR:   cir.const #cir.const_record<{#cir.int<13> : !s32i, #cir.int<17> : !s64i}> : !rec_Agg

// LLVM-LABEL: @_Z17test_retAgg_firstv
// LLVM:   store %struct.Agg { i32 13, i64 17 }, ptr %{{.*}}, align 8

// OGCG-LABEL: @_Z17test_retAgg_firstv
// OGCG:   store i32 13, ptr %{{.*}}, align 8
// OGCG:   store i64 17, ptr %{{.*}}, align 8
