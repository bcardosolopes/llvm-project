// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=OGCG

// Test that vtable pointer initialization is emitted in destructors of
// dynamic non-final classes. Conservative fix: always init
// vtable pointers for dynamic non-final classes.

struct FieldWithDtor {
  ~FieldWithDtor();
};

struct Base {
  virtual void f();
  virtual ~Base();
};

// Dynamic, non-final class with a field that has a non-trivial destructor.
// The vtable pointer must be re-initialized in ~Derived before destroying
// fields, so any virtual calls during field destruction resolve correctly.
struct Derived : Base {
  FieldWithDtor field;
  ~Derived() {}  // trivial body, but has non-trivial field destruction
};

Derived d;

// Check that the base destructor (D2) initializes the vtable pointer.
// CIR-LABEL: cir.func {{.*}} @_ZN7DerivedD2Ev
// CIR:         cir.vtable.address_point(@_ZTV7Derived

// LLVM-LABEL: define {{.*}} @_ZN7DerivedD2Ev
// LLVM:         store {{.*}} @_ZTV7Derived

// OGCG-LABEL: define {{.*}} @_ZN7DerivedD2Ev
// OGCG:         store {{.*}} @_ZTV7Derived
