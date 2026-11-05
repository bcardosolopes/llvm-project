// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c11 -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c11 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c11 -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG

// Test that _Atomic types with padding are correctly represented as
// a struct containing the value type and a padding array.

struct SmallStruct {
  char a, b, c;
};

// SmallStruct is 3 bytes, _Atomic(SmallStruct) is 4 bytes => 1 byte padding.
_Atomic(struct SmallStruct) g_padded_atomic;

// The anonymous struct wraps {SmallStruct, [1 x u8]} for atomic padding.
// CIR: !rec_anon_struct = !cir.record<struct {!rec_SmallStruct, !cir.array<!u8i x 1>}>
// CIR: cir.global external @g_padded_atomic = #cir.zero : !rec_anon_struct

// LLVM: @g_padded_atomic = global { %struct.SmallStruct, [1 x i8] }
// OGCG: @g_padded_atomic = global { %struct.SmallStruct, [1 x i8] }
