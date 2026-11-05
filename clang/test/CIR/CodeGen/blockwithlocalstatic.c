// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -fblocks -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefix=LLVM --check-prefix=COMMON
// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -fblocks -emit-llvm -o - %s | FileCheck %s --check-prefix=OGCG --check-prefix=COMMON

int (^block)(void) = ^ {
  static int test = 0;
  return test;
};

void (^block1)(void) = ^ {
  static int test = 2;
  return;
};

int (^block2)(void) = ^ {
  static int test = 5;
  return test;
};

// COMMON-DAG: @{{.*}}test = internal global i32 0
// COMMON-DAG: @{{.*}}test = internal global i32 2
// COMMON-DAG: @{{.*}}test = internal global i32 5

// LLVM-LABEL: define internal i32 @block_block_invoke.invoke(ptr noundef %{{.*}})
// LLVM: load i32, ptr @{{.*}}test, align 4

// OGCG-LABEL: define internal i32 @block_block_invoke(ptr noundef %{{.*}})
// OGCG: load i32, ptr @block_block_invoke.test
