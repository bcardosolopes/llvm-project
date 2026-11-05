// RUN: %clang_cc1 -triple x86_64-linux-gnu -std=c++17 -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s
// RUN: %clang_cc1 -triple x86_64-linux-gnu -std=c++17 -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

// CIR loads vtable from the unadjusted this pointer instead of the
// adjusted subobject pointer when dispatching virtual calls through member
// function pointers. The Itanium ABI requires loading the vtable from the
// adjusted pointer (after applying memptr.adj).

struct A {
  virtual void foo();
  int a;
};

struct B {
  virtual void bar();
  int b;
};

struct C : A, B {
  void foo() override;
  void bar() override;
};

// When calling through a member function pointer to A on a C object cast to B*,
// the adjustment is non-zero. The vtable must be loaded from the adjusted pointer.
void call_through_memptr(C *c, void (A::*fp)()) {
  (c->*fp)();
}

// The adjusted this pointer is computed and the vtable is loaded from it.
// CHECK-LABEL: define {{.*}} @_Z19call_through_memptrP1CM1AFvvE
// CHECK: [[ADJ_THIS:%[0-9]+]] = getelementptr inbounds i8, ptr %{{.*}}, i64 %
// CHECK: load ptr, ptr [[ADJ_THIS]]

// OGCG-LABEL: define {{.*}} @_Z19call_through_memptrP1CM1AFvvE
// OGCG: [[ADJ_THIS:%[a-z.0-9]+]] = getelementptr inbounds i8, ptr %{{.*}}, i64 %
// OGCG: load ptr, ptr [[ADJ_THIS]]
