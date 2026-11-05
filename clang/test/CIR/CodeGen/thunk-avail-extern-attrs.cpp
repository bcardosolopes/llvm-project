// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -fstrict-vtable-pointers \
// RUN:   -std=c++11 -disable-llvm-passes -O2 -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -fstrict-vtable-pointers \
// RUN:   -std=c++11 -disable-llvm-passes -O2 -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Verify that available_externally thunks do NOT get nonnull/align/
// dereferenceable on the 'this' parameter. OGCG calls
// SetLLVMFunctionAttributes with IsThunk=true which skips these attrs.

struct NotTrivialDtor {
  ~NotTrivialDtor();
};
struct DynamicBase1 {
  NotTrivialDtor obj;
  virtual void foo();
};
struct DynamicBase2 {
  virtual void bar();
  ~DynamicBase2() { bar(); }
};
struct DynamicDerivedMultiple : DynamicBase1, DynamicBase2 {
  void foo() override;
  void bar() override;
};

void test() {
  DynamicDerivedMultiple DDM;
  DDM.foo();
  DDM.bar();
}

// The available_externally thunk should have plain 'ptr noundef':
// CIR: define available_externally void @_ZThn16_N22DynamicDerivedMultiple3barEv(ptr noundef
// CIR-NOT: nonnull
// CIR-NOT: dereferenceable
// CIR-SAME: )

// OGCG: define available_externally void @_ZThn16_N22DynamicDerivedMultiple3barEv(ptr noundef
// OGCG-NOT: nonnull
// OGCG-NOT: dereferenceable
// OGCG-SAME: )
