// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Regression test: member pointer casts with sugared types used to crash.
//
// When a member pointer type like `int Derived::*` is sugar for
// `int Middle::*` (because `y` is actually declared in Middle), the CIR
// codegen must use the canonical class (Middle) -- not the sugar class
// (Derived) -- when computing the base class offset for the cast path.
// Previously, CIR extracted the class from the non-canonical qualifier,
// causing an assertion failure in getBaseClassOffset when the cast path
// didn't match the (wrong) derived class's direct bases.

struct Base { int x; };
struct Middle : public Base { int y; };
struct Derived final : public Middle { int z; };

// CK_DerivedToBaseMemberPointer: int Derived::* -> int Base::*
// The sugar type says Derived, but canonical type is Middle.
// Path is Middle -> Base (1 step).
// CHECK-LABEL: define {{.*}}void @_Z29test_derived_to_base_data_ptrM7Derivedi(
void test_derived_to_base_data_ptr(int Derived::*p) {
  int Base::*q = static_cast<int Base::*>(p);
  (void)q;
}
// Just check it compiles without crashing; the cast may be folded away
// at -O0.
// CHECK: ret void

// CK_BaseToDerivedMemberPointer: int Base::* -> int Derived::*
// CHECK-LABEL: define {{.*}}void @_Z29test_base_to_derived_data_ptrM4Basei(
void test_base_to_derived_data_ptr(int Base::*p) {
  int Derived::*q = static_cast<int Derived::*>(p);
  (void)q;
}
// CHECK: ret void
