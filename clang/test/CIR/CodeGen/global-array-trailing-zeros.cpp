// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Test that global arrays with trailing zeros coalesce the zero portion
// using zeroinitializer instead of expanding every element.

// Small array — 3 nonzero, 120 trailing zeros
int sparse_array[123] = {1, 2, 3};

// CIR: cir.global {{.*}} @sparse_array = #cir.const_array<[#cir.int<1> : !s32i, #cir.int<2> : !s32i, #cir.int<3> : !s32i], trailing_zeros> : !cir.array<!s32i x 123>

// LLVM: @sparse_array = global <{ i32, i32, i32, [120 x i32] }> <{ i32 1, i32 2, i32 3, [120 x i32] zeroinitializer }>

// OGCG: @sparse_array = global <{ i32, i32, i32, [120 x i32] }> <{ i32 1, i32 2, i32 3, [120 x i32] zeroinitializer }>

// Larger array — 1 nonzero, 999 trailing zeros
int big_array[1000] = {42};

// CIR: cir.global {{.*}} @big_array = #cir.const_array<[#cir.int<42> : !s32i], trailing_zeros> : !cir.array<!s32i x 1000>

// LLVM: @big_array = global <{ i32, [999 x i32] }> <{ i32 42, [999 x i32] zeroinitializer }>

// OGCG: @big_array = global <{ i32, [999 x i32] }> <{ i32 42, [999 x i32] zeroinitializer }>

// All-zero array — should use ZeroAttr
int zero_array[100] = {};

// CIR: cir.global {{.*}} @zero_array = #cir.zero : !cir.array<!s32i x 100>

// LLVM: @zero_array = global [100 x i32] zeroinitializer

// OGCG: @zero_array = global [100 x i32] zeroinitializer
