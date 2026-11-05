// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fcxx-exceptions \
// RUN:   -fexceptions -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fcxx-exceptions \
// RUN:   -fexceptions -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Regression test: TryMarkNoThrow must not transitively propagate
// nounwind from callee definitions. A function calling a non-noexcept lambda
// (whose body is trivially nounwind) should NOT be marked nounwind, matching
// OGCG behavior.

int call_lambda() {
  auto l = []() { return 42; };
  return l();
}

// CIR output has lambda before caller (CIR emission order).
// The caller should NOT have nounwind:
// CIR-DAG: define {{.*}} @_Z11call_lambdav() #[[CALLER_ATTR:[0-9]+]]
// CIR-DAG: define internal {{.*}} @"_ZZ11call_lambdavENK3$_0clEv"({{.*}}) #[[LAMBDA_ATTR:[0-9]+]]
// CIR: attributes #[[LAMBDA_ATTR]] = {{{.*}}nounwind{{.*}}}
// CIR-NOT: attributes #[[CALLER_ATTR]] = {{{.*}}nounwind{{.*}}}

// OGCG output has caller before lambda (OGCG emission order).
// OGCG: define {{.*}} @_Z11call_lambdav() #[[CALLER_ATTR:[0-9]+]]
// OGCG: define internal {{.*}} @"_ZZ11call_lambdavENK3$_0clEv"({{.*}}) #[[LAMBDA_ATTR:[0-9]+]]
// OGCG: attributes #[[LAMBDA_ATTR]] = {{{.*}}nounwind{{.*}}}
// OGCG-NOT: attributes #[[CALLER_ATTR]] = {{{.*}}nounwind{{.*}}}
