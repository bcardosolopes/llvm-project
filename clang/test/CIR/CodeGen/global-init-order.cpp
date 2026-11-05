// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=OGCG

// Test that CIR matches OGCG's global initialization ordering for deferred
// variables. When variables with internal linkage are deferred (not emitted
// eagerly) and then triggered for emission out of declaration order, the init
// functions in _GLOBAL__sub_I_ must still respect the TU declaration order.
//
// This is: without the fix, CIR would call z's init before y's init,
// reading uninitialized memory.

int f();

// x is emitted eagerly (calls f() which has side effects).
static int x = f();

// y and z are deferred (internal linkage, no side effects in decl).
// z depends on y, and y depends on x.
static int y = x - 1;
static int z = ~y;

// test() uses z, which triggers z's emission first. z's init references y,
// so y is also emitted. But y was declared before z, so y must be initialized
// before z in _GLOBAL__sub_I_.
int test() { return z; }

// Both OGCG and CIR must call y's init before z's init.
// The function .1 is z's init (emitted first), .2 is y's init (emitted second).
// In _GLOBAL__sub_I_, the order must be .2 (y) before .1 (z).

// LLVM: define internal void @__cxx_global_var_init() {{.*}}section ".text.startup" {
// LLVM:   call noundef i32 @_Z1fv()
// LLVM:   store i32 %{{.*}}, ptr @_ZL1x

// z's init function is emitted first (triggered by test()):
// LLVM: define internal void @__cxx_global_var_init.1() {{.*}}section ".text.startup" {
// LLVM:   load i32, ptr @_ZL1y
// LLVM:   xor i32 %{{.*}}, -1
// LLVM:   store i32 %{{.*}}, ptr @_ZL1z

// y's init function is emitted second:
// LLVM: define internal void @__cxx_global_var_init.2() {{.*}}section ".text.startup" {
// LLVM:   load i32, ptr @_ZL1x
// LLVM:   sub nsw i32 %{{.*}}, 1
// LLVM:   store i32 %{{.*}}, ptr @_ZL1y

// _GLOBAL__sub_I_ must call .2 (y) before .1 (z):
// LLVM: define internal void @_GLOBAL__sub_I_{{.*}}() {{.*}}section ".text.startup" {
// LLVM-NEXT:   call void @__cxx_global_var_init()
// LLVM-NEXT:   call void @__cxx_global_var_init.2()
// LLVM-NEXT:   call void @__cxx_global_var_init.1()
// LLVM-NEXT:   ret void

// OGCG ordering must match:
// OGCG: define internal void @_GLOBAL__sub_I_{{.*}}()
// OGCG:   call void @__cxx_global_var_init()
// OGCG-NEXT:   call void @__cxx_global_var_init.2()
// OGCG-NEXT:   call void @__cxx_global_var_init.1()
// OGCG-NEXT:   ret void
