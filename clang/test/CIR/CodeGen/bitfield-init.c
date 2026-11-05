// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG

// Verify that global struct initializers with _Bool bitfields work correctly.
// The _Bool initializer produces a BoolAttr that must be converted to IntAttr
// for bitfield layout.

struct s5 {
  unsigned f0 : 2;
  _Bool f1 : 1;
  _Bool f2 : 1;
};

// f0=3 (0b11), f1=1 (0b1), f2=0 (0b0) => byte 0b00001111 = 7 (little-endian)
// CIR: cir.global external @g5
// LLVM: @g5 = global %struct.s5 { i8 7, [3 x i8] zeroinitializer }, align 4
// OGCG: @g5 = global %struct.s5 { i8 7, [3 x i8] zeroinitializer }, align 4
struct s5 g5 = { 3, 1 };

struct s1 {
  unsigned f0 : 4;
  _Bool f1 : 1;
};

// f0=0xA (0b1010), f1=1 (0b1) => byte 0b00011010 = 26
// CIR: cir.global external @g1
// LLVM: @g1 = global %struct.s1 { i8 26, [3 x i8] zeroinitializer }, align 4
// OGCG: @g1 = global %struct.s1 { i8 26, [3 x i8] zeroinitializer }, align 4
struct s1 g1 = { 0xA, 1 };
