// RUN: %clang_cc1 -triple x86_64-unknown-linux -flto -flto-unit \
// RUN:   -fvisibility=hidden -fvirtual-function-elimination \
// RUN:   -fwhole-program-vtables -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux -flto -flto-unit \
// RUN:   -fvisibility=hidden -fvirtual-function-elimination \
// RUN:   -fwhole-program-vtables -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM

// Test that -fvirtual-function-elimination with -fwhole-program-vtables
// lowers virtual calls through llvm.type.checked.load like OGCG.

struct A {
  virtual void foo();
};

void test(A *p) {
  p->foo();
}

// CIR-LABEL: @_Z4testP1A
// CIR: %{{.+}} = cir.load
// CIR: cir.call %{{.+}}

// LLVM-LABEL: define{{.*}} void @_Z4testP1A
// LLVM: load ptr, ptr %{{.+}}
// LLVM: %[[VTABLE:.+]] = load ptr, ptr %{{.+}}
// LLVM: %[[PAIR:.+]] = call { ptr, i1 } @llvm.type.checked.load(ptr %[[VTABLE]], i32 0, metadata !"_ZTS1A")
// LLVM-NEXT: %[[FN:.+]] = extractvalue { ptr, i1 } %[[PAIR]], 0
// LLVM-NEXT: %[[OK:.+]] = extractvalue { ptr, i1 } %[[PAIR]], 1
// LLVM-NOT: @llvm.type.test
// LLVM-NOT: @llvm.assume
// LLVM: call void %[[FN]](ptr
