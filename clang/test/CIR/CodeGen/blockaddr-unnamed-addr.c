// RUN: %clang_cc1 -triple x86_64-unknown-linux -fclangir -emit-llvm -o - %s \
// RUN:   | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux -emit-llvm -o - %s \
// RUN:   | FileCheck %s

// Verify that private constant globals containing blockaddress values
// get unnamed_addr, matching OGCG.

void test(int x) {
  const void *labels[] = {&&a, &&b, &&c};
  goto *labels[x];
a: return;
b: return;
c: return;
}

// CHECK: @__const.test.labels = private unnamed_addr constant [3 x ptr]
