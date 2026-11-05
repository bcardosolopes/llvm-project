// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Verify that llvm.compiler.used does not contain duplicate entries for
// decomposition bindings with __attribute__((used)).

export module Foo;

int ary[2];

namespace N {
static auto &__attribute__((used))[e, f] = ary;
}

// CHECK: @llvm.compiler.used = appending global [1 x ptr] [ptr @_ZN1NDC1e1fEE], section "llvm.metadata"
