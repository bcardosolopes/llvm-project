// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck %s --input-file %t-cir.ll
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -emit-llvm %s -o %t.ll
// RUN: FileCheck %s -check-prefix=OGCG --input-file %t.ll

// Verify that forward-declared (incomplete) struct types are emitted as
// "type opaque" rather than "type {}" (empty struct).

struct FwdDeclared;

extern thread_local struct DestructedFwdDecl e3;

// CHECK: %struct.DestructedFwdDecl = type opaque
// OGCG: %struct.DestructedFwdDecl = type opaque

void *get_addr() { return &e3; }
