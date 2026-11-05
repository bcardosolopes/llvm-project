// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -std=c++17 %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -std=c++17 %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

// Verify that incomplete array types are fixed up based on actual
// initializer element count.

extern const int arr[];
const int arr[] = {10, 20, 30, 40};

// CIR: cir.global constant external @arr = #cir.const_array<[#cir.int<10> : !s32i, #cir.int<20> : !s32i, #cir.int<30> : !s32i, #cir.int<40> : !s32i]> : !cir.array<!s32i x 4>
// LLVM: @arr = constant [4 x i32] [i32 10, i32 20, i32 30, i32 40]

struct Pair { int a, b; };
extern const Pair pairs[];
const Pair pairs[] = {{1, 2}, {3, 4}, {5, 6}};

// CIR: cir.global constant external @pairs = #cir.const_array<[#cir.const_record<{#cir.int<1> : !s32i, #cir.int<2> : !s32i}> : !rec_Pair, #cir.const_record<{#cir.int<3> : !s32i, #cir.int<4> : !s32i}> : !rec_Pair, #cir.const_record<{#cir.int<5> : !s32i, #cir.int<6> : !s32i}> : !rec_Pair]> : !cir.array<!rec_Pair x 3>
// LLVM: @pairs = constant [3 x %struct.Pair] [%struct.Pair { i32 1, i32 2 }, %struct.Pair { i32 3, i32 4 }, %struct.Pair { i32 5, i32 6 }]
