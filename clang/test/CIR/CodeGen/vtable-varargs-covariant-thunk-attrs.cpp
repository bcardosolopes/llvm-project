// Verify that CIR emits correct function attributes on variadic covariant
// return thunks. OGCG's GenerateVarArgsThunk uses llvm::CloneFunction which
// copies all function attributes from the base function; CIR must do the same.
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

struct Base1 {
  virtual Base1* clone(...) = 0;
};

struct Base2 {
  virtual ~Base2() {}
};

struct Derived : public Base2, public Base1 {
  virtual Derived* clone(...);
};

Derived* Derived::clone(...) {
  return new Derived();
}

// The covariant thunk must have mustprogress and min-legal-vector-width
// attributes matching the base function (inherited via function cloning).

// CHECK: define {{.*}} @_ZTchn8_h8_N7Derived5cloneEz({{.*}}) unnamed_addr [[THUNK_ATTRS:#[0-9]+]]
// CHECK: attributes [[THUNK_ATTRS]] = {
// CHECK-SAME: mustprogress
// CHECK-SAME: "min-legal-vector-width"="0"
// CHECK-SAME: }

// OGCG: define {{.*}} @_ZTchn8_h8_N7Derived5cloneEz({{.*}}) unnamed_addr [[THUNK_ATTRS:#[0-9]+]]
// OGCG: attributes [[THUNK_ATTRS]] = {
// OGCG-SAME: mustprogress
// OGCG-SAME: "min-legal-vector-width"="0"
// OGCG-SAME: }
