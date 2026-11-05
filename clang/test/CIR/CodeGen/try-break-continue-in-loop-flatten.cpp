// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -fexceptions -fcxx-exceptions %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM

// A break/continue inside a cir.try nested in a loop previously drove
// CIRFlattenCFGPass into unbounded RewriterBase::eraseOp recursion (a compiler
// stack overflow): after loop flattening lowered the continue/break to a cir.br
// escaping the enclosing cir.scope, the greedy driver erased that now-"dead"
// scope, and eraseOp's cross-region CFG post-order block walk followed the
// escaping branch back to the scope op forever. The loop flattening must inline
// such escaping-branch scopes (innermost-out, walking nested scopes) instead of
// leaving them for the greedy driver to erase. These functions must compile
// without crashing and produce a well-formed loop.

void continue_in_try_in_loop() {
  for (int i = 0; i < 3; ++i) {
    try { continue; } catch (int) {}
  }
}

// The continue lowers to a back-edge through the step (increment), which returns
// to the condition; the loop is intact and nothing is dropped.
// LLVM-LABEL: define {{.*}}@_Z23continue_in_try_in_loopv
// LLVM: icmp slt i32 {{.*}}, 3
// LLVM: add nsw i32 {{.*}}, 1
// LLVM: ret void

void break_in_try_in_loop() {
  for (int i = 0; i < 3; ++i) {
    try { break; } catch (int) {}
  }
}

// The break lowers to a branch to the loop exit.
// LLVM-LABEL: define {{.*}}@_Z20break_in_try_in_loopv
// LLVM: icmp slt i32 {{.*}}, 3
// LLVM: ret void
