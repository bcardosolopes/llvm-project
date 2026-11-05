// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -fcxx-exceptions -fexceptions -O2 %s -o %t.ll
// RUN: FileCheck %s --input-file=%t.ll

// Regression test: when a function has two disjoint control-flow paths — one
// that allocates memory via operator new (with a throwing constructor cleanup)
// and another that catches an exception by reference (causing __cxa_end_catch
// to potentially throw) — the lowering must not clone the new-expression's
// cleanup block for the __cxa_end_catch landing pad. The cloned cleanup would
// reference the operator-new result, which does not dominate the landing pad,
// causing "Instruction does not dominate all uses!" verifier errors.

struct Obj {
  Obj(const char *);
  ~Obj();
};

extern void *gbl;

// CHECK-LABEL: define {{.*}}void @_Z4testb(
void test(bool cond) {
  if (cond) {
    gbl = new Obj("x");
    __builtin_abort();
  }
  try {
    throw 42;
  } catch (Obj &e) {
  }
}

// The key check: the operator delete (_ZdlPvm) for the new expression must
// only appear in the cleanup landing pad that is dominated by the _Znwm call,
// never in a landing pad reachable from the catch-by-reference path.
//
// CHECK: call {{.*}}@_Znwm(
// CHECK: invoke void @_ZN3ObjC1EPKc(
// CHECK: landingpad
// CHECK-NEXT: cleanup
// CHECK-NEXT: {{.*}}call void @_ZdlPvm(
//
// The __cxa_end_catch in the catch handler must NOT have a cleanup landing pad
// that references the _Znwm result. It should either be a plain call or an
// invoke to a terminate handler — but never an invoke to a cleanup that
// calls _ZdlPvm with the new-expression pointer.
//
// CHECK: call {{.*}}@__cxa_begin_catch(
// CHECK: call void @__cxa_end_catch()
// CHECK-NEXT: ret void
