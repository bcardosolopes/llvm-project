// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fclangir -emit-mlir=cir-abi -mmlir -mlir-print-ir-before=cir-call-conv-lowering %s -o %t.cir 2> %t-before.cir
// RUN: FileCheck --input-file=%t-before.cir %s

// Boolean vector alloca alignment should be based on storage size.
// <8 x i1> = 1 byte → align 1, not align 8.

typedef bool __attribute__((ext_vector_type(8))) bool8;
bool8 global_v;

// The global for <8 x bool> should have alignment 1 (1 byte storage).
// CHECK: cir.global {{.*}} @global_v {{.*}} {alignment = 1 : i64}
