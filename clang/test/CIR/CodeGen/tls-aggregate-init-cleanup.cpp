// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -fcxx-exceptions -fexceptions -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -fcxx-exceptions -fexceptions -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG

// Verify that thread_local aggregate-initialized variables with members
// that have non-trivial destructors don't produce domination errors.
// The RunCleanupsScope around emitInitAndDtor() in the non-threadsafe
// guard path ensures EH cleanups are popped inside the guard scope.

struct D {
    D() {}
    ~D() {}
};

struct S {
    D d;
};

void sink(S*);

void f() {
    thread_local S s{};
    sink(&s);
}

// CIR: cir.func {{.*}} @_Z1fv
// CIR:   cir.if
// CIR:     cir.call @_ZN1DC1Ev
// CIR:     cir.call @__cxa_thread_atexit
// CIR:   }
// CIR:   cir.call @_Z4sinkP1S
// CIR-NOT: cir.try
// CIR:   cir.return

// LLVM: define {{.*}} @_Z1fv
// LLVM:   call void @_ZN1DC1Ev
// LLVM:   call {{.*}} @__cxa_thread_atexit
// LLVM:   call void @_Z4sinkP1S
// LLVM:   ret void

// OGCG: define {{.*}} @_Z1fv
// OGCG:   call void @_ZN1DC1Ev
// OGCG:   call {{.*}} @__cxa_thread_atexit
// OGCG:   call void @_Z4sinkP1S
// OGCG:   ret void
