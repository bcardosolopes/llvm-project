// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// Verify that __cxa_throw and __cxa_rethrow call-sites have the noreturn
// attribute, matching OGCG.

struct A { int x; };

// throw outside try — becomes call (not invoke)
// CHECK-LABEL: @_Z11throw_plainv
// CHECK: call void @__cxa_throw({{.*}}) [[NORETURN:#[0-9]+]]
// OGCG-LABEL: @_Z11throw_plainv
// OGCG: call void @__cxa_throw({{.*}}) [[OGCG_NORETURN:#[0-9]+]]
void throw_plain() { throw A{42}; }

// throw inside try — becomes invoke
// CHECK-LABEL: @_Z12throw_in_tryv
// CHECK: invoke void @__cxa_throw({{.*}}) [[NORETURN]]
// OGCG-LABEL: @_Z12throw_in_tryv
// OGCG: invoke void @__cxa_throw({{.*}}) [[OGCG_NORETURN]]
int throw_in_try() {
  try { throw A{1}; } catch (...) { return 1; }
  return 0;
}

// rethrow inside catch — becomes invoke
// CHECK-LABEL: @_Z10rethrow_itv
// CHECK: invoke void @__cxa_rethrow() [[NORETURN]]
// OGCG-LABEL: @_Z10rethrow_itv
// OGCG: invoke void @__cxa_rethrow() [[OGCG_NORETURN]]
void rethrow_it() {
  try { throw 42; } catch (...) { throw; }
}

// CHECK: attributes [[NORETURN]] = { noreturn }
// OGCG: attributes [[OGCG_NORETURN]] = { noreturn }
