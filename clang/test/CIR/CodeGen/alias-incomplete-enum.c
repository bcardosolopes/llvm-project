// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -emit-llvm -o - %s | FileCheck %s --check-prefix=OGCG

// LLVM: @test13_alias ={{.*}} alias {}, ptr @test13
// OGCG: @test13_alias ={{.*}} alias {}, ptr @test13

enum a_type { test13_a };
void test13(enum a_type y) {}
void test13_alias(enum undeclared_type y) __attribute__((alias("test13")));
