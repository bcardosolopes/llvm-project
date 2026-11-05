// RUN: %clang_cc1 -std=c++20 -triple x86_64-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -std=c++20 -triple x86_64-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM

// Verify that class-type NTTPs (C++20) work correctly.
// Previously crashed with "emitDeclRefLValue: unhandled decl type".

struct S { int x; };
template<S s> int f() { return s.x; }

int main() { return f<S{42}>(); }

// CIR: cir.global {{.*}}constant linkonce_odr comdat @_ZTAXtl1SLi42EEE = #cir.const_record<{#cir.int<42> : !s32i}> : !rec_S

// LLVM: @_ZTAXtl1SLi42EEE = linkonce_odr constant %struct.S { i32 42 }, comdat
