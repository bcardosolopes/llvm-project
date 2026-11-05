// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o /dev/null 2>&1
//
// Previously crashed on module-exported structured binding declaration.
// Fixed by adding Decl::Binding case to emitTopLevelDecl.

export module Foo;

int ary[2];

export auto &[a, b] = ary;
