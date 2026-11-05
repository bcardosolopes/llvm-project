// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

// Test that trivial copy/move constructors for empty base classes are skipped.
// Empty classes have size 1 in C++ but occupy 0 bytes as base subobjects due
// to empty base optimization (EBO). Emitting a copy for them would generate a
// spurious 1-byte memcpy that corrupts overlapping data at offset 0.
//
// This matches OGCG behavior where EmitAggregateCopy checks isEmpty() and
// returns early for empty C++ records.

struct Empty {};

struct NonTrivialBase {
  int *data;
  NonTrivialBase() : data(nullptr) {}
  NonTrivialBase(const NonTrivialBase &other);
};

struct Derived : NonTrivialBase, Empty {
  // Implicit copy constructor should:
  // 1. Call NonTrivialBase's copy constructor (non-trivial)
  // 2. Skip Empty's copy constructor (trivial + empty)
};

void test_copy(const Derived &src) {
  Derived dst(src);
}

// The copy constructor for Derived should NOT contain a cir.copy for
// the Empty base class (which would generate a spurious 1-byte memcpy).

// CIR-LABEL: cir.func {{.*}} @_ZN7DerivedC2ERKS_(
// CIR:         cir.call @_ZN14NonTrivialBaseC2ERKS_(
// CIR-NOT:     cir.copy
// CIR:         cir.return

// At the LLVM IR level, verify no memcpy is emitted (only the
// NonTrivialBase copy constructor call).
// LLVM-LABEL: define {{.*}} @_ZN7DerivedC2ERKS_(
// LLVM:         call void @_ZN14NonTrivialBaseC2ERKS_(
// LLVM-NOT:     llvm.memcpy
// LLVM:         ret void
