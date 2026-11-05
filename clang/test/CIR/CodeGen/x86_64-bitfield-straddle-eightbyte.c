// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o - | FileCheck %s

// A 16-byte aligned(16) struct whose lowered bitfield storage (a 64-bit unit at
// byte offset 4) straddles the eightbyte boundary must be classified as two
// INTEGER eightbytes and coerced to { i64, i64 }, not a single i64. Before the
// fix the CIR x86-64 classifier only marked the eightbyte containing the
// storage unit's start, dropping the second eightbyte (so the type was
// passed/returned as a lone i64), which corrupted the trailing bitfield across
// a by-value copy. This mirrors OGCG, which classifies each bitfield's bit-span
// and sets Hi=Integer for bits that land in the second eightbyte.

struct S0 {
  unsigned short m0[2];
  short m1 : 9;
  short m2 : 11;
  short m3 : 12;
} __attribute__((aligned(16)));

struct S0 rt(struct S0 v) { return v; }

// CHECK-LABEL: define dso_local { i64, i64 } @rt(i64 %{{[0-9]+}}, i64 %{{[0-9]+}})
