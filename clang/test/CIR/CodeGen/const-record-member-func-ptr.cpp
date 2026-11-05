// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fclangir -emit-cir -mmlir -mlir-print-ir-before=cir-cxxabi-lowering %s -o %t.cir 2> %t-before.cir
// RUN: FileCheck --check-prefix=CIR-BEFORE --input-file=%t-before.cir %s
// RUN: FileCheck --check-prefix=CIR-AFTER --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ll %s

// Member function pointers nested in a named record's global initializer.
// The record members are rewritten in place before conversion, so the
// global must be judged (and its initializer lowered) by initializer
// content, not just by whether its type changed.

struct D {
  struct E { unsigned char m, v, l; bool (D::*r)(const unsigned char *, unsigned &, unsigned, bool); };
  static const E Ring[];
  bool op0(const unsigned char *, unsigned &, unsigned, bool);
  bool op1(const unsigned char *, unsigned &, unsigned, bool);
};
const D::E D::Ring[] = {{0, 0, 0, &D::op0}, {1, 1, 1, &D::op1}};

// CIR-BEFORE: ![[$E:.*]] = !cir.record<struct "D::E" {!u8i, !u8i, !u8i, !cir.method<{{.*}}> in {{.*}}}>
// CIR-BEFORE: cir.global {{.*}} @_ZN1D4RingE = #cir.const_array<[#cir.const_record<{#cir.int<0> : !u8i, #cir.int<0> : !u8i, #cir.int<0> : !u8i, #cir.method<@_ZN1D3op0EPKhRjjb>

// CIR-AFTER: ![[$ANON:.*]] = !cir.record<struct  {!s64i, !s64i}>
// CIR-AFTER: ![[$E:.*]] = !cir.record<struct "D::E" {!u8i, !u8i, !u8i, ![[$ANON]]}>
// CIR-AFTER: cir.global {{.*}} @_ZN1D4RingE = #cir.const_array<[#cir.const_record<{#cir.int<0> : !u8i, #cir.int<0> : !u8i, #cir.int<0> : !u8i, #cir.const_record<{#cir.global_view<@_ZN1D3op0EPKhRjjb> : !s64i, #cir.int<0> : !s64i}> : ![[$ANON]]}> : ![[$E]]

// LLVM: @_ZN1D4RingE = {{.*}}constant [2 x %"struct.D::E"] [%"struct.D::E" { i8 0, i8 0, i8 0, { i64, i64 } { i64 ptrtoint (ptr @_ZN1D3op0EPKhRjjb to i64), i64 0 } }, %"struct.D::E" { i8 1, i8 1, i8 1, { i64, i64 } { i64 ptrtoint (ptr @_ZN1D3op1EPKhRjjb to i64), i64 0 } }]
// OGCG: @_ZN1D4RingE = {{.*}}constant [2 x %"struct.D::E"] [%"struct.D::E" { i8 0, i8 0, i8 0, { i64, i64 } { i64 ptrtoint (ptr @_ZN1D3op0EPKhRjjb to i64), i64 0 } }, %"struct.D::E" { i8 1, i8 1, i8 1, { i64, i64 } { i64 ptrtoint (ptr @_ZN1D3op1EPKhRjjb to i64), i64 0 } }]
