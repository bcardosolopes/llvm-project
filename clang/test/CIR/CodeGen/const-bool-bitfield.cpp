// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

// Test 1: _Bool bitfield members in constant APValue evaluation.
// Previously BoolAttr was not handled in the APValue build() path,
// causing constant emission to silently fail for structs with _Bool
// bitfield members.

struct BoolBitfield {
  bool a : 1;
  bool b : 1;
  int c : 6;
};

// The record type should be marked padded (3 bytes of tail padding
// after the bitfield byte).
// CIR: !cir.record<struct "BoolBitfield" padded

// CIR: cir.global {{.*}} @_ZL2bf = #cir.const_record
// CIR-SAME: !rec_BoolBitfield
// LLVM: @_ZL2bf = {{.*}}constant %struct.BoolBitfield
constexpr BoolBitfield bf = {true, false, 42};

int read_bf() { return bf.a + bf.b + bf.c; }
