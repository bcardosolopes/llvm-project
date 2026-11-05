// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -fcxx-exceptions -fexceptions -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG

// A `goto` that jumps to a label lexically inside a switch stays within the
// switch's init-statement / condition-variable scope. The switch variable must
// therefore be destroyed exactly once on the goto path (at the merged end of
// the switch scope), NOT a second time at the point of the goto. Previously,
// emitSwitchStmt's outer LexicalScope had no source range recorded, so
// emitGotoStmt's forward-goto walk assumed the goto exited the scope and
// emitted a spurious second destructor, double-freeing the switch variable
// (heap corruption). This is the switch sibling of emitIfStmt's fix;
// see CIRGenStmt.cpp emitSwitchStmt.

struct S {
  int value;
  S();
  ~S();
  explicit operator bool() const;
};

void may_throw();

// A goto to an in-switch label from inside the switch: the init variable `s`
// is destroyed exactly once, at the switch scope exit, not at the goto.
void goto_into_switch(int x) {
  switch (S s{}; x) {
  case 0:
    goto label;
  default:
  label:
    (void)s.value;
  }
}

// LLVM-LABEL: define {{.*}} @_Z16goto_into_switchi(
// LLVM-NOT: call void @_ZN1SD1Ev(
// LLVM: call void @_ZN1SD1Ev(
// LLVM-NEXT: ret void

// OGCG-LABEL: define {{.*}} @_Z16goto_into_switchi(
// OGCG-NOT: call void @_ZN1SD1Ev(
// OGCG: call void @_ZN1SD1Ev(
// OGCG: ret void

// A goto out of a catch handler to a label inside the enclosing switch's
// condition-variable scope: again `s` is destroyed exactly once.
void goto_catch_into_switch(int x) {
  switch (S s{}; x) {
  case 0:
    try {
      may_throw();
    } catch (...) {
      goto label;
    }
    break;
  default:
  label:
    (void)s.value;
  }
}

// LLVM-LABEL: define {{.*}} @_Z22goto_catch_into_switchi(
// LLVM: call ptr @__cxa_begin_catch(
// LLVM: invoke void @__cxa_end_catch()
// LLVM-NOT: call void @_ZN1SD1Ev(
// LLVM: call void @_ZN1SD1Ev(
// LLVM-NEXT: ret void

// Classic codegen (OGCG) emits the switch-variable destructor on each disjoint
// exit edge (the goto-through-cleanup path and the normal path), so there is
// more than one textual dtor call; the key invariant checked here is that the
// normal exit destroys `s` once immediately before returning. The clangir LLVM
// checks above assert the stronger property (a single dtor on the goto path).
// OGCG-LABEL: define {{.*}} @_Z22goto_catch_into_switchi(
// OGCG: call ptr @__cxa_begin_catch(
// OGCG: invoke void @__cxa_end_catch()
// OGCG: call void @_ZN1SD1Ev(
// OGCG: call void @_ZN1SD1Ev(
// OGCG: ret void
