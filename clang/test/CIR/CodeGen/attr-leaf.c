// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu %s -fclangir -emit-cir -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu %s -fclangir -emit-llvm -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu %s -emit-llvm -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Test that __attribute__((leaf)) maps to nocallback.

void __attribute__((leaf)) foo(void);

void bar(void) {
  foo();
}

// CIR-DAG: #[[CIR_CALL_ATTR:fn_attr[0-9]*]] = #cir<extra({nocallback})>
// CIR-DAG: #[[CIR_DECL_ATTR:fn_attr[0-9]*]] = #cir<extra({{.*}}nocallback{{.*}})>
// CIR: cir.call @foo() : () -> () extra(#[[CIR_CALL_ATTR]])
// CIR: cir.func private @foo() extra(#[[CIR_DECL_ATTR]])
// LLVM: call void @foo() #[[LLVM_CALL_ATTR:[0-9]+]]
// LLVM: declare void @foo() #[[LLVM_DECL_ATTR:[0-9]+]]
// LLVM: attributes #[[LLVM_DECL_ATTR]] = { nocallback
// LLVM: attributes #[[LLVM_CALL_ATTR]] = { nocallback }
// LLVM-NOT: "nocallback"
// OGCG: call void @foo() #[[OGCG_CALL_ATTR:[0-9]+]]
// OGCG: declare void @foo() #[[OGCG_DECL_ATTR:[0-9]+]]
// OGCG: attributes #[[OGCG_DECL_ATTR]] = { nocallback
// OGCG: attributes #[[OGCG_CALL_ATTR]] = { nocallback }
// OGCG-NOT: "nocallback"
