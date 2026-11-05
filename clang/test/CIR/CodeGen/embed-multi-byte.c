// RUN: %clang_cc1 -std=c23 -triple x86_64-unknown-linux-gnu -Wno-excess-initializers -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -std=c23 -triple x86_64-unknown-linux-gnu -Wno-excess-initializers -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -std=c23 -triple x86_64-unknown-linux-gnu -Wno-excess-initializers -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=OGCG

// Test that #embed correctly preserves all embedded bytes in array
// initializers, not just the first byte (regression test).

// The file starts with "//", so the first two bytes are 0x2F, 0x2F (47, 47)
// and the third byte is a space 0x20 (32).

struct S {
  int arr[3];
};

// Multi-byte #embed in int array — all embedded bytes must be preserved.
void embed_int_array() {
  int b[2] = {
#embed __FILE__
  };
}

// Multi-byte #embed in char array.
void embed_char_array() {
  char d[2] = {
#embed __FILE__
  };
}

// Multi-byte #embed in struct initializer.
void embed_struct() {
  struct S s = {
#embed __FILE__
  };
}

// CIR-DAG: cir.global "private" {{.*}} @__const.embed_int_array.b = #cir.const_array<[#cir.int<47> : !s32i, #cir.int<47> : !s32i]> : !cir.array<!s32i x 2>
// CIR-DAG: cir.global "private" {{.*}} @__const.embed_char_array.d = #cir.const_array<"//" : !cir.array<!s8i x 2>>
// CIR-DAG: cir.global "private" {{.*}} @__const.embed_struct.s = #cir.const_record<{#cir.const_array<[#cir.int<47> : !s32i, #cir.int<47> : !s32i, #cir.int<32> : !s32i]> : !cir.array<!s32i x 3>}>

// LLVM-DAG: @__const.embed_int_array.b = private unnamed_addr constant [2 x i32] [i32 47, i32 47]
// LLVM-DAG: @__const.embed_char_array.d = private unnamed_addr constant [2 x i8] c"//"
// LLVM-DAG: @__const.embed_struct.s = private unnamed_addr constant %struct.S { [3 x i32] [i32 47, i32 47, i32 32] }

// OGCG-DAG: @__const.embed_int_array.b = private unnamed_addr constant [2 x i32] [i32 47, i32 47]
// OGCG-DAG: @__const.embed_char_array.d = private unnamed_addr constant [2 x i8] c"//"
// OGCG-DAG: @__const.embed_struct.s = private unnamed_addr constant %struct.S { [3 x i32] [i32 47, i32 47, i32 32] }
