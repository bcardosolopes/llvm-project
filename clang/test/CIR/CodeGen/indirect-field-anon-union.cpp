// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s -check-prefix=OGCG

// Test that IndirectField declarations (anonymous union members at namespace
// scope) are handled by emitTopLevelDecl without crashing. The IndirectField
// itself needs no codegen — the data is emitted via the parent anonymous record.

namespace test {
  static union {
    unsigned bar[4096] __attribute__((aligned(128)));
  };
  unsigned *getBar() { return bar; }
}

// LLVM: @_ZN4test3barE = internal global %union.anon{{.*}} zeroinitializer, align 128
// LLVM: define dso_local noundef ptr @_ZN4test6getBarEv()

// OGCG: @_ZN4test3barE = internal global %union.anon zeroinitializer, align 128
// OGCG: define dso_local noundef ptr @_ZN4test6getBarEv()
