// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -std=c++17 -fcxx-exceptions -fexceptions %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm \
// RUN:   -std=c++17 -fcxx-exceptions -fexceptions %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=OGCG

// Test that placement new with a non-global placement delete operator
// correctly generates the EH cleanup to call the matching delete.

inline void* operator new(unsigned long, void* p) noexcept { return p; }

namespace test12 {
  struct A {
    void operator delete(void *, void *);
    A();
  };

  A *test(void *ptr) {
    return new (ptr) A();
  }
}

// Both CIR and OGCG should invoke the constructor and have a cleanup
// that calls the placement delete if the constructor throws.
// LLVM-LABEL: @_ZN6test124testEPv
// LLVM: invoke void @_ZN6test121AC1Ev
// LLVM: cleanup

// OGCG-LABEL: @_ZN6test124testEPv
// OGCG: invoke void @_ZN6test121AC1Ev
// OGCG: cleanup
