// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s -check-prefix=CIRLLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM

struct Struk {
  ~Struk();
  bool check();
};

// Test that a for-loop body containing a local variable with a destructor
// followed by 'continue' and 'return' produces a properly terminated
// cir.scope. This exercises the interaction between LexicalScope cleanup,
// CleanupScopeOp popping, and empty-block erasure.

int test_cleanup_return_in_loop(int n) {
  for (int i = 0; i < n; i++) {
    Struk s;
    if (s.check())
      continue;
    return i;
  }
  return -1;
}

// CIR: cir.func {{.*}} @_Z27test_cleanup_return_in_loopi
// CIR:       cir.scope {
// CIR:         cir.for : cond {
// CIR:         } body {
// CIR:           cir.scope {
// CIR:             cir.cleanup.scope {
// CIR:               cir.continue
// CIR:               cir.return
// CIR:             } cleanup normal {
// CIR:               cir.call @_ZN5StrukD1Ev
// CIR:               cir.yield
// CIR:         cir.yield
// CIR:         } step {
// CIR:       cir.return

// LLVM: define dso_local noundef i32 @_Z27test_cleanup_return_in_loopi(i32 noundef %{{.*}})
// LLVM: [[LOOP_COND:.*]]:
// LLVM:     %[[ICMP:.*]] = icmp slt i32
// LLVM-NEXT: br i1 %[[ICMP]]
// LLVM: [[LOOP_BODY:.*]]:
// LLVM:     %[[CALL:.*]] = call noundef {{.*}} @_ZN5Struk5checkEv(
// LLVM-NEXT: br i1 %[[CALL]]
// LLVM: [[CLEANUP:.*]]:
// LLVM:     call void @_ZN5StrukD1Ev(
// LLVM:     switch i32 %{{.*}}, label %{{.*}} [
// LLVM:     ]
// LLVM: [[LOOP_STEP:.*]]:
// LLVM:     add nsw i32 %{{.*}}, 1
// LLVM: [[POST_LOOP:.*]]:
// LLVM:     store i32 -1, ptr %{{.*}}, align 4
// LLVM:     ret i32

// CIR duplicates the cleanup once per exit instead of dispatching on a
// cleanup destination slot like classic codegen does above.
// CIRLLVM: define dso_local noundef i32 @_Z27test_cleanup_return_in_loopi(i32 noundef %{{.*}})
// CIRLLVM: [[LOOP_COND:.*]]:
// CIRLLVM:     %[[ICMP:.*]] = icmp slt i32
// CIRLLVM-NEXT: br i1 %[[ICMP]]
// CIRLLVM: [[LOOP_BODY:.*]]:
// CIRLLVM:     %[[CALL:.*]] = call noundef {{.*}} @_ZN5Struk5checkEv(
// CIRLLVM-NEXT: br i1 %[[CALL]]
// CIRLLVM: [[CONTINUE_CLEANUP:.*]]:
// CIRLLVM:     call void @_ZN5StrukD1Ev(
// CIRLLVM: [[RETURN_CLEANUP:.*]]:
// CIRLLVM:     call void @_ZN5StrukD1Ev(
// CIRLLVM:     ret i32
// CIRLLVM: [[LOOP_STEP:.*]]:
// CIRLLVM:     add nsw i32 %{{.*}}, 1
// CIRLLVM: [[POST_LOOP:.*]]:
// CIRLLVM:     store i32 -1, ptr %{{.*}}, align 4
// CIRLLVM:     ret i32
