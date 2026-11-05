// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s -check-prefix=OGCG

// Test that ternary expressions with void type on both branches
// compile correctly (regression test). The crash occurred
// when both branches were "cheap enough to evaluate unconditionally"
// (e.g. __builtin_assume(true) and void()) because CIR tried to
// create a zero initializer for void type.

bool e();

void operator delete(void*);

// LLVM-LABEL: define {{.*}}void @_Z1fPiz(
// OGCG-LABEL: define {{.*}}void @_Z1fPiz(
void f(int* p, ...)
{
  // Branching path: __builtin_trap() is not cheap to evaluate.
  e() ? __builtin_trap() : void();
  // LLVM: call noundef zeroext i1 @_Z1ev()
  // LLVM: call void @llvm.trap()
  // OGCG: call noundef zeroext i1 @_Z1ev()
  // OGCG: call void @llvm.trap()

  // Branching path: __builtin_debugtrap() is not cheap.
  e() ? __builtin_debugtrap() : void();
  // LLVM: call noundef zeroext i1 @_Z1ev()
  // LLVM: call void @llvm.debugtrap()
  // OGCG: call noundef zeroext i1 @_Z1ev()
  // OGCG: call void @llvm.debugtrap()

  // Unconditional path: __builtin_assume(true) and void() are both cheap.
  // This was the crash case.
  e() ? __builtin_assume(true) : void();
  // LLVM: call noundef zeroext i1 @_Z1ev()
  // LLVM: call void @llvm.assume(i1 true)
  // OGCG: call noundef zeroext i1 @_Z1ev()
  // OGCG: call void @llvm.assume(i1 true)

  // va_start in ternary.
  __builtin_va_list l;
  e() ? __builtin_va_start(l, p) : void();
  // LLVM: call void @llvm.va_start
  // OGCG: call void @llvm.va_start

  // va_copy in ternary.
  __builtin_va_list l2;
  e() ? __builtin_va_copy(l, l2) : void();
  // LLVM: call void @llvm.va_copy
  // OGCG: call void @llvm.va_copy

  // prefetch in ternary.
  e() ? __builtin_prefetch(p) : void();
  // LLVM: call void @llvm.prefetch
  // OGCG: call void @llvm.prefetch

  // operator delete in ternary.
  e() ? __builtin_operator_delete(p) : void();
  // LLVM: call void @_ZdlPv
  // OGCG: call void @_ZdlPv
}
