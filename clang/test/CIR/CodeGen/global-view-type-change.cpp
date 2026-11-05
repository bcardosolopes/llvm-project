// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir -std=c++20
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll -std=c++20
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

// Regression test: createNewGlobalView must handle GlobalViewAttr
// type conversions when a global's type changes from incomplete to complete.
// This exercises the code path where a GlobalViewAttr with indices references
// a global that is later re-created with a different type (e.g., incomplete
// array to complete array).

// Forward-declare an incomplete array.
extern int table[];

// A global initializer referencing table with an index offset.
// This creates a GlobalViewAttr with indices [2].
int *element = &table[2];

// Define table — type changes from int[] (incomplete) to int[5] (complete).
// createNewGlobalView must update the GlobalViewAttr for the new global.
int table[] = {10, 20, 30, 40, 50};

// CIR: cir.global {{.*}} @table = #cir.const_array<[#cir.int<10>
// CIR: cir.global {{.*}} @element = #cir.global_view<@table, [2 : i32]> : !cir.ptr<!s32i>

// LLVM: @table = global [5 x i32] [i32 10, i32 20, i32 30, i32 40, i32 50]
// LLVM: @element = global ptr getelementptr (i8, ptr @table, i64 8)
