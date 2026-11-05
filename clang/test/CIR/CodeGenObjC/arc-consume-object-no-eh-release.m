// A CK_ARCConsumeObject value (here a +1 ns_returns_retained result passed to a
// +0 parameter) is released at end-of-full-expression. Under default -fobjc-arc
// (no -fobjc-arc-exceptions), classic CodeGen pushes this cleanup with
// getARCCleanupKind() == NormalCleanup, so the release runs only on the normal
// path -- never on the EH unwind path. CIR previously hardcoded
// NormalAndEHCleanup, emitting an extra objc_release on the landing-pad/resume
// path, which is an over-release/UAF when unwinding.
//
// RUN: %clang_cc1 -triple arm64-apple-ios16.0.0 -fobjc-arc -fexceptions \
// RUN:   -fobjc-runtime=ios-16.0.0 -fclangir -emit-llvm %s -o - \
// RUN:   | FileCheck %s --check-prefix=DEFAULT
//
// With -fobjc-arc-exceptions the release must run on both the normal and the
// unwind paths, matching classic.
// RUN: %clang_cc1 -triple arm64-apple-ios16.0.0 -fobjc-arc -fobjc-arc-exceptions \
// RUN:   -fexceptions -fobjc-runtime=ios-16.0.0 -fclangir -emit-llvm %s -o - \
// RUN:   | FileCheck %s --check-prefix=EH
// RUN: %clang_cc1 -triple arm64-apple-ios16.0.0 -fobjc-arc -fobjc-arc-exceptions \
// RUN:   -fexceptions -fobjc-runtime=ios-16.0.0 -fclangir -emit-llvm %s -o - \
// RUN:   | FileCheck %s --check-prefix=EHREL

__attribute__((objc_root_class))
@interface Obj
@end
@implementation Obj
@end

extern __attribute__((ns_returns_retained)) Obj *makeRetained(void);
extern int mayThrow(void);
extern void sink(Obj *a, int b);

void f(void) {
  sink(makeRetained(), mayThrow());
}

// Default: no EH edges at all -- the potentially-throwing calls stay plain
// calls, and the consumed value is released exactly once on the normal path.
// DEFAULT-LABEL: define void @f()
// DEFAULT-NOT:   invoke
// DEFAULT-NOT:   landingpad
// DEFAULT:       call void @llvm.objc.release(
// DEFAULT:       ret void
// DEFAULT-NOT:   call void @llvm.objc.release(
// DEFAULT-NOT:   resume

// With arc-exceptions the calls become invokes with a cleanup landing pad, and
// the release runs on both the normal-return path and the unwind/resume path.
// How many landing pads survive and in what order the blocks come out is up to
// FlattenCFG, so match the structure without pinning a block layout.
// EH-LABEL: define void @f()
// EH-DAG:       invoke i32 @mayThrow()
// EH-DAG:       landingpad
// EH-DAG:       resume
// EH-DAG:       ret void

// Exactly two releases: one per path, never a third.
// EHREL-LABEL: define void @f()
// EHREL-COUNT-2: call void @llvm.objc.release(
// EHREL-NOT:     call void @llvm.objc.release(
// EHREL:         {{^}}}
