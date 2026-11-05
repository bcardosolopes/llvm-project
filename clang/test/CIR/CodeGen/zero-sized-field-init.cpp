// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
//
// Regression test: ConstStructBuilder should not
// crash on zero-sized fields (empty base classes, [[no_unique_address]]).

struct Empty {};

struct HasEmpty {
    [[no_unique_address]] Empty e;
    int x;
};

// Verify constant initialization works with zero-sized fields.
HasEmpty g = {{}, 42};

// CHECK: @g = {{.*}}global {{.*}} i32 42
