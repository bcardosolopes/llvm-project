// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR %s --input-file=%t.cir
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM %s --input-file=%t.ll

// DesignatedInitUpdateExpr on a struct with bitfield members now folds to a
// constant by updating the underlying byte storage.

struct Bitfield {
  int a : 3;
  int b : 4;
  int c : 5;
};
struct WithBitfield {
  int n;
  Bitfield b;
};

WithBitfield bf = {1, (Bitfield){2, 3, 4}, .b.b = 5};

// CIR: cir.global external @bf = #cir.const_record<{#cir.int<1> : !s32i, #cir.const_record<{#cir.int<42> : !u8i, #cir.int<2> : !u8i, #cir.undef : !cir.array<!u8i x 2>}> : !rec_anon_struct}> : !rec_anon_struct1
// CIR-NOT: cir.func no_inline internal private @__cxx_global_var_init

// LLVM: @bf = global { i32, { i8, i8, [2 x i8] } } { i32 1, { i8, i8, [2 x i8] } { i8 42, i8 2, [2 x i8] undef } }, align 4
// LLVM-NOT: define internal void @__cxx_global_var_init
