// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM

// Regression test: __scoped_atomic_thread_fence previously aborted ClangIR with
// a (misleadingly named "unimplemented X86 builtin call") NYI. It now maps the
// memory-scope argument via the generic AtomicScopeModel and reuses the
// __atomic_thread_fence lowering (cir.atomic.fence -> llvm fence).

void sysfence(void) {
  __scoped_atomic_thread_fence(__ATOMIC_SEQ_CST, __MEMORY_SCOPE_SYSTEM);
}
// CIR-LABEL: cir.func {{.*}}@sysfence
// CIR: cir.atomic.fence syncscope(system) seq_cst
// LLVM-LABEL: @sysfence
// LLVM: fence seq_cst
