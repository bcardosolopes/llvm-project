// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s
// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Init priority grouping. Same-priority inits are grouped into
// _GLOBAL__I_NNNNNN wrapper functions, sorted by priority.

void foo(int);

class A {
public:
  A() { foo(1); }
};

class B {
public:
  B() { foo(2); }
};

A a __attribute__((init_priority(300)));
A a1 __attribute__((init_priority(300)));
B b __attribute__((init_priority(200)));

// OGCG groups the two priority-300 inits into one wrapper:
// CHECK: @llvm.global_ctors = appending global [{{[0-9]+}} x { i32, ptr, ptr }]
// CHECK-SAME: i32 200
// CHECK-SAME: i32 300
// The priority 200 entry should come before 300:
// CHECK-NOT: i32 300{{.*}}i32 200

// OGCG: @llvm.global_ctors = appending global [{{[0-9]+}} x { i32, ptr, ptr }]
// OGCG-SAME: i32 200
// OGCG-SAME: i32 300
// OGCG-NOT: i32 300{{.*}}i32 200
