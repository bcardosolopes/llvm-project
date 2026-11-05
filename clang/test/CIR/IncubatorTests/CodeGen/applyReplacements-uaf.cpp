// RUN: %clang_cc1 -std=c++20 -triple aarch64-unknown-linux-gnu -fexceptions -fcxx-exceptions -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
//
// Regression test for stack-use-after-scope in
// CIRGenModule::applyReplacements(). A perf optimization (cache the last 1-2
// resolved root SymbolRefs) declared cache state as
// stack locals inside each `if (nameMap.size() == N)` branch and registered a
// lambda that captured them by reference. The single
// `replacer.recursivelyReplaceElementsIn(theModule)` invocation lived OUTSIDE
// the if/else chain — by the time the walker called the lambda, the captured
// locals had already gone out of scope, so reads landed on freed stack slots.
// On builds where the freed slots happened to hold zeros the issue stayed
// hidden; on the prebuilt clangir-next binary it crashed downstream during
// CallConvLowering with a use-after-free on the now-corrupt SymbolRefAttr.
//
// The fix moves recursivelyReplaceElementsIn into every branch so the
// captured locals stay live until the lambda has finished its walk.
//
// This synthetic repro produces several polymorphic classes with virtual
// dtors. The Itanium ABI emits both base (D2) and complete (D1) dtor variants
// and sometimes records D2-as-alias-for-D1 replacements in
// CIRGenModule::replacements. The vtables emitted for these classes contain
// FlatSymbolRefAttr entries to the dtor symbols, so `applyReplacements()`
// walks them and invokes the registered replacement lambda — exactly the
// path that triggered the UAF.

struct A {
  virtual ~A();
  virtual void f();
};
struct B : A {
  ~B() override;
  void f() override;
};
struct C : A {
  ~C() override;
  void f() override;
};
struct D : A {
  ~D() override;
  void f() override;
};

// CHECK-LABEL: define{{.*}} void @_Z3useP1A
void use(A *a) { a->f(); }

// Force vtable emission for each derived class via a "use" function.
// CHECK-LABEL: define{{.*}} void @_Z5makesv
void makes() {
  A a;
  B b;
  C c;
  D d;
  use(&a);
  use(&b);
  use(&c);
  use(&d);
}
