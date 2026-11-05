// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck %s --input-file=%t.cir
//
// Verify that calling a builtin function in a global dynamic initializer
// does not crash. Previously, emitDirectCallee would cast curFn to FuncOp,
// but during global init curFn is a GlobalOp.

extern "C" int abs(int);

int x = abs(-42);

// CHECK: cir.global
// CHECK: cir.func
