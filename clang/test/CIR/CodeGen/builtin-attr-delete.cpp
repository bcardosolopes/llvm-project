// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -std=c++20 %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -std=c++20 %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

// Verify that operator delete call sites get the `builtin` attribute
// when the declaration has `nobuiltin`. Matches OGCG CGExprCXX.cpp:1348-1352.

struct T { ~T(); };
void f(T *t) { delete t; }

// CIR-DAG: #[[DEL_ATTR:fn_attr[0-9]*]] = #cir<extra({builtin, nothrow = #cir.nothrow})>
// CIR: cir.call @_ZdlPvm({{.*}}) nothrow {{.*}} extra(#[[DEL_ATTR]])

// LLVM: call void @_ZdlPvm({{.*}}) #[[CALL_ATTRS:[0-9]+]]
// LLVM: attributes #[[CALL_ATTRS]] = { builtin nounwind }
