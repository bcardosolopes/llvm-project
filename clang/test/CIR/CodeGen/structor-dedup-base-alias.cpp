// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -O1 -disable-llvm-passes -mconstructor-aliases -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -O1 -disable-llvm-passes -mconstructor-aliases -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t-cir.ll %s
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -O1 -disable-llvm-passes -mconstructor-aliases -emit-llvm %s -o %t-ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t-ogcg.ll %s

// The stubs that get folded must be gone from every output, and CIR must agree
// with OGCG on which ones those are.
// RUN: FileCheck --check-prefix=FOLDED --input-file=%t.cir %s
// RUN: FileCheck --check-prefix=FOLDED --input-file=%t-cir.ll %s
// RUN: FileCheck --check-prefix=FOLDED --input-file=%t-ogcg.ll %s

// Without -mconstructor-aliases nothing is aliased and nothing is folded.
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -O1 -disable-llvm-passes -fclangir -emit-cir %s -o %t-noalias.cir
// RUN: FileCheck --check-prefix=NOALIAS --input-file=%t-noalias.cir %s

// tryEmitBaseDestructorAsAlias points a derived class's D2 at its base's D2
// when the derived destructor adds nothing of its own. That leaves D1 as a stub
// delegating across a class boundary, and CIRGen retypes the `this` argument at
// that call to match the new callee -- so the delegation reaches
// LoweringPrepare's dedup with a `cir.cast bitcast` in it. The bitcast is only
// ever a retype here: the aliasing is offered only when the base sits at offset
// zero, and an inheritance step that actually moves the pointer would be a
// `cir.base_class_addr`, which the dedup still refuses.
//
// This is deliberately checked at -O1: tryEmitBaseDestructorAsAlias declines at
// -O0 to keep the two destructors distinguishable in the debugger.

struct KeyBase {
  virtual ~KeyBase();
  virtual void f();
};
KeyBase::~KeyBase() {}
void KeyBase::f() {}

//===----------------------------------------------------------------------===//
// D1 delegating to a base class's D2 through a bitcast
//===----------------------------------------------------------------------===//

struct Inherited : KeyBase {
  void f() override;
};
void Inherited::f() {}

// Both of Inherited's non-deleting destructors are gone: D2 because it aliases
// KeyBase's, D1 because it does nothing but forward to D2.

// CIR: cir.global {{.*}}@_ZTV9Inherited = #cir.vtable<{{{.*}}#cir.global_view<@_ZN7KeyBaseD2Ev>{{.*}}#cir.global_view<@_ZN9InheritedD0Ev>{{.*}}}>
// LLVM: @_ZTV9Inherited = {{.*}}ptr @_ZN7KeyBaseD2Ev, ptr @_ZN9InheritedD0Ev
// OGCG: @_ZTV9Inherited = {{.*}}ptr @_ZN7KeyBaseD2Ev, ptr @_ZN9InheritedD0Ev

// FOLDED-NOT: _ZN9InheritedD1Ev
// FOLDED-NOT: _ZN9InheritedD2Ev

// NOALIAS-DAG: cir.func {{.*}} @_ZN9InheritedD1Ev
// NOALIAS-DAG: cir.func {{.*}} @_ZN9InheritedD2Ev
// NOALIAS-DAG: cir.global {{.*}}@_ZTV9Inherited = #cir.vtable<{{{.*}}#cir.global_view<@_ZN9InheritedD1Ev>{{.*}}}>

//===----------------------------------------------------------------------===//
// Two non-trivial bases: no aliasing, but the plain delegation still folds
//===----------------------------------------------------------------------===//

struct OtherBase {
  virtual ~OtherBase();
  virtual void g();
};
OtherBase::~OtherBase() {}
void OtherBase::g() {}

struct Two : OtherBase, KeyBase {
  void f() override;
  void g() override;
};
void Two::f() {}
void Two::g() {}

// There is no unique base to alias to, so Two::D2 is emitted for real and
// Two::D1 forwards to it with no cast -- the delegation the dedup already
// handled. The KeyBase-subobject thunk is a real function and survives.

// CIR-DAG: cir.func {{.*}} @_ZN3TwoD2Ev
// CIR-DAG: cir.func {{.*}} @_ZThn8_N3TwoD1Ev
// CIR-DAG: cir.global {{.*}}@_ZTV3Two = #cir.vtable<{{{.*}}#cir.global_view<@_ZN3TwoD2Ev>{{.*}}#cir.global_view<@_ZThn8_N3TwoD1Ev>{{.*}}}>
// LLVM-DAG: define linkonce_odr void @_ZN3TwoD2Ev(ptr
// LLVM-DAG: @_ZTV3Two = {{.*}}ptr @_ZN3TwoD2Ev, ptr @_ZN3TwoD0Ev{{.*}}ptr @_ZThn8_N3TwoD1Ev
// OGCG-DAG: define linkonce_odr void @_ZN3TwoD2Ev(ptr
// OGCG-DAG: @_ZTV3Two = {{.*}}ptr @_ZN3TwoD2Ev, ptr @_ZN3TwoD0Ev{{.*}}ptr @_ZThn8_N3TwoD1Ev

// FOLDED-NOT: _ZN3TwoD1Ev

// NOALIAS-DAG: cir.func {{.*}} @_ZN3TwoD1Ev
