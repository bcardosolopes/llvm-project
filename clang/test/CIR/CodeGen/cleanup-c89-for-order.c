// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -std=c89 %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=C89
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm \
// RUN:   -std=c89 %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=OGCG

// Test that cleanup call ordering in C89 mode matches OGCG. In C89,
// for-loop init-declared variables have the scope of the enclosing
// block, so i = 5 should happen BEFORE j's cleanup.

void cleaner(int *p);

void test_c89_cleanup_order(void) {
  for (int i = 10; 0;) {
    for (__attribute__((cleanup(cleaner))) int j = 20; 0;)
      ;
    i = 5;
  }
}

// Both should: store i32 5 BEFORE call void @cleaner
// C89: store i32 5
// C89: call void @cleaner
// OGCG: store i32 5
// OGCG: call void @cleaner
