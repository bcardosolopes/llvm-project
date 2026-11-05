// RUN: %clang_cc1 -fclangir -emit-llvm -triple x86_64-unknown-linux-gnu %s -o %t.ll
// RUN: FileCheck %s --input-file=%t.ll

// Test that extern function declarations get target-features attributes,
// matching OGCG behavior.

extern int callee(void);

int test(void) {
  return callee();
}

// The callee declaration should have target-features.
// CHECK: declare {{.*}} @callee() #[[DECL_ATTRS:[0-9]+]]
// CHECK: attributes #[[DECL_ATTRS]] = {{{.*}}"target-features"=
