// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG

// Global variable decomposition (structured bindings at file scope).

struct S { int a, b; };

S make();

auto [x, y] = make();

// CIR: cir.global external @_ZDC1x1yE = #cir.zero : !rec_S
// CIR: cir.func no_inline internal private @__cxx_global_var_init()
// CIR:   cir.get_global @_ZDC1x1yE
// CIR:   cir.call @_Z4makev()

// LLVM: @_ZDC1x1yE = global %struct.S zeroinitializer
// LLVM: define internal void @__cxx_global_var_init()

// OGCG: @_ZDC1x1yE = global %struct.S zeroinitializer
// OGCG: define internal void @__cxx_global_var_init()
