// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -o - %s \
// RUN:   | FileCheck --check-prefix=LLVM %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -emit-llvm -o - %s \
// RUN:   | FileCheck --check-prefix=OGCG %s

// When a ternary expression's result is bound to a const reference
// parameter, CIR must not destroy the temporary inside the ternary branch.
// The temporary's lifetime extends to the end of the full expression (after
// the function call returns). Both CIR and OGCG use cleanup.cond guards
// and destructor calls AFTER the call to use().

struct A {
  A();
  ~A();
  int value() const;
};

struct B : A {};
struct C : A {};

void use(const A&);

// Both branches produce a const A& through derived-to-base conversion.
// The temporary's lifetime extends to the end of the full expression.
void test(bool b) {
  use(b ? static_cast<const A&>(B()) : static_cast<const A&>(C()));
}

// Destructor calls must come AFTER use():
// LLVM-LABEL: define {{.*}}@_Z4testb
// LLVM: call void @_Z3useRK1A
// LLVM: call void @_ZN1CD1Ev
// LLVM: call void @_ZN1BD1Ev

// OGCG-LABEL: define {{.*}}@_Z4testb
// OGCG: call void @_Z3useRK1A
// OGCG: call void @_ZN1CD1Ev
// OGCG: call void @_ZN1BD1Ev
