// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -fcxx-exceptions -fexceptions %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -emit-llvm -fcxx-exceptions -fexceptions %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=OGCG

// Test: destructor function-try-block with throw in try body and return in
// catch handler. The implicit noexcept spec pushes a Terminate scope on the
// EH scope stack, which is a non-cleanup scope. The emitCleanupsForEarlyExit
// walk must not pass below this scope via the normal cleanup chain (which
// only links through EHCleanupScopes and skips Terminate/Catch scopes).

struct A { A(); ~A(); };
struct B : A {
  A member;
  ~B() try { throw 1; } catch (...) { return; }
};
B b;

// Verify the destructor compiles and has the right structure:
// - Calls __cxa_throw in the try body
// - Landing pad catches all exceptions
// - Calls member/base destructors
// - Calls __cxa_begin_catch / __cxa_end_catch
// - Returns (no implicit rethrow because of explicit return)

// LLVM-LABEL: define {{.*}} @_ZN1BD1Ev
// LLVM: call ptr @__cxa_allocate_exception
// LLVM: invoke void @__cxa_throw
// LLVM: landingpad
// LLVM: call void @_ZN1AD1Ev
// LLVM: call void @_ZN1AD2Ev
// LLVM: call ptr @__cxa_begin_catch
// LLVM: ret void

// OGCG-LABEL: define {{.*}} @_ZN1BD1Ev
// OGCG: call ptr @__cxa_allocate_exception
// OGCG: invoke void @__cxa_throw
// OGCG: landingpad
// OGCG: call void @_ZN1AD1Ev
// OGCG: call void @_ZN1AD2Ev
// OGCG: call ptr @__cxa_begin_catch
// OGCG: __cxa_end_catch
// OGCG: ret void
