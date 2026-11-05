// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=OGCG

// Empty struct member after bitfields should not be part of CIR record
// layout, matching OGCG. Previously crashed with insertPadding assertion
// `offset >= size` because the empty member overlapped bitfield tail padding.

struct Empty {};

struct P7 {
    unsigned a : 16;
    unsigned b : 8;
    Empty e;
    unsigned c;
} p7;

// CIR: !{{.*}} = !cir.record<struct "P7" {!u32i, !u32i}>

// The Empty member should not appear in the LLVM struct type.
// LLVM: %struct.P7 = type { i32, i32 }
// OGCG: %struct.P7 = type { i32, i32 }
