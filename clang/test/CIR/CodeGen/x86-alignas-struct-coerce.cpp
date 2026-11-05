// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -fclangir-call-conv-lowering -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Test that alignas structs with small data members are coerced correctly.
// Previously, CIR classified tail padding as INTEGER, causing wrong coercion.

// alignas(16) with single int: should return i32, not {i64, i64}
struct alignas(16) Aligned16 { int x; };
Aligned16 make_aligned16();

// CHECK-LABEL: @_Z13use_aligned16v
// CHECK: call i32 @_Z14make_aligned16v()
void use_aligned16() { Aligned16 a = make_aligned16(); (void)a.x; }

// Regular struct with padding but no alignas: should still return i64
struct Padded8 { int a; short b; };
Padded8 make_padded8();

// CHECK-LABEL: @_Z11use_padded8v
// CHECK: call i64 @_Z12make_padded8v()
void use_padded8() { Padded8 p = make_padded8(); (void)p.a; }
