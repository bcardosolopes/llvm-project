// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir \
// RUN:   -ftrivial-auto-var-init=zero %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -ftrivial-auto-var-init=zero %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm \
// RUN:   -ftrivial-auto-var-init=zero %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=OGCG

// Verify that constant aggregate local variable initialization works
// correctly with -ftrivial-auto-var-init=zero.

struct S { int a; int b; };

void test_const_agg_init(void) {
  struct S s = {1, 2};
}

// CIR-LABEL: cir.func {{.*}}@test_const_agg_init
// CIR:   cir.get_global @__const.test_const_agg_init.s
// CIR:   cir.copy

// LLVM: define {{.*}}@test_const_agg_init
// LLVM:   call void @llvm.memcpy{{.*}}@__const.test_const_agg_init.s

// OGCG: define {{.*}}@test_const_agg_init
// OGCG:   call void @llvm.memcpy{{.*}}@__const.test_const_agg_init.s

struct T { int a; char b; int c; };

void test_partial_init(void) {
  struct T t = {1, 'x'};
}

// CIR-LABEL: cir.func {{.*}}@test_partial_init
// CIR:   cir.get_global @__const.test_partial_init.t
// CIR:   cir.copy

// LLVM: define {{.*}}@test_partial_init
// LLVM:   call void @llvm.memcpy{{.*}}@__const.test_partial_init.t

// OGCG: define {{.*}}@test_partial_init
// OGCG:   call void @llvm.memcpy{{.*}}@__const.test_partial_init.t
