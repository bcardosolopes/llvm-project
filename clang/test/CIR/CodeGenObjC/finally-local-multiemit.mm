// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -emit-llvm -fexceptions -fobjc-exceptions -fobjc-runtime-has-weak -fclangir -o - %s | FileCheck %s

// A local declared inside an @finally body must not trip the symbol-table
// ("not supposed to be available just yet") or LocalDeclMap ("Decl already
// exists in LocalDeclMap!") assertions when the @try also has a @catch. CIRGen
// re-emits the @finally body once per edge leaving the protected region (a
// normal fall-through copy and an exceptional/unwind copy), so each textual
// copy must isolate its locals in a fresh symbol-table + LocalDeclMap scope.
// This is the @try/@catch/@finally sibling of objc-finally-local.m, whose
// coverage was limited to the no-@catch (numHandlers == 0) path.

struct Guard {
  Guard();
  ~Guard();
};
void mayThrow();
int counter();

// CHECK-DAG: @[[STORAGE:_ZZ27finallyStaticLocalWithCatchvE1s]] = internal global i32 0
// CHECK-DAG: @[[GUARD:_ZGVZ27finallyStaticLocalWithCatchvE1s]] = internal global

// A non-static C++ local in the @finally body. The normal-exit copy and the
// exceptional copy each get their own alloca, and each copy constructs and
// destroys the local exactly once (balanced, on its own slot).
void finallyLocalWithCatch() {
  @try { mayThrow(); }
  @catch (id e) { mayThrow(); }
  @finally { Guard g; (void)g; }
}

// CHECK-LABEL: define{{.*}} void @_Z21finallyLocalWithCatchv
// The two @finally copies use two distinct Guard slots (fresh per-copy storage).
// CHECK: %[[EXC:[0-9]+]] = alloca %struct.Guard
// CHECK: %[[NORM:[0-9]+]] = alloca %struct.Guard
// Normal fall-through copy: construct then destroy its own slot.
// CHECK: call void @_ZN5GuardC1Ev(ptr {{.*}} %[[NORM]])
// CHECK-NEXT: call void @_ZN5GuardD1Ev(ptr {{.*}} %[[NORM]])
// Exceptional copy: a distinct slot, constructed via invoke (may throw during
// unwind) and destroyed on the unwind path.
// CHECK: invoke void @_ZN5GuardC1Ev(ptr {{.*}} %[[EXC]])
// CHECK: call void @_ZN5GuardD1Ev(ptr {{.*}} %[[EXC]])

// A static local in the @finally body. Its storage and guard are a single
// shared global referenced by every re-emitted copy (no duplicate global, no
// LocalDeclMap collision); every copy guards initialization with that one
// guard so the initializer runs at most once at runtime.
void finallyStaticLocalWithCatch() {
  @try { mayThrow(); }
  @catch (id e) { mayThrow(); }
  @finally { static int s = counter(); (void)s; }
}

// CHECK-LABEL: define{{.*}} void @_Z27finallyStaticLocalWithCatchv
// CHECK: call i32 @__cxa_guard_acquire(ptr @[[GUARD]])
// CHECK: store i32 {{.*}}, ptr @[[STORAGE]]
// CHECK: call void @__cxa_guard_release(ptr @[[GUARD]])
