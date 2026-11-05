// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir --check-prefix=CIR %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

// Test: __attribute__((not_tail_called)) sets notail on calls to the function.

__attribute__((not_tail_called)) int callee(int x);

int caller(int x) {
  return callee(x);
}

// CIR: #[[NOTAIL_ATTR:.*]] = #cir<extra({no_tail})>
// CIR-LABEL: cir.func {{.*}} @caller
// CIR: cir.call @callee({{.*}}) {{.*}} extra(#[[NOTAIL_ATTR]])

// LLVM-LABEL: define {{.*}} @caller
// LLVM: notail call {{.*}} @callee

// OGCG-LABEL: define {{.*}} @caller
// OGCG: notail call {{.*}} @callee
