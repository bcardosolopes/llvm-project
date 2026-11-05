// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=DEFAULT --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -no-enable-noundef-analysis %s -o %t-no.ll
// RUN: FileCheck --check-prefix=DISABLED --input-file=%t-no.ll %s

// Test that -no-enable-noundef-analysis suppresses noundef attributes.

void foo(int x);
void test(int n) { foo(n); }

// DEFAULT: define {{.*}}@test(i32 noundef
// DISABLED: define {{.*}}@test(i32 %
