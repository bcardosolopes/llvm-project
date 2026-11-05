// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fcxx-exceptions \
// RUN:   -fexceptions -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fcxx-exceptions \
// RUN:   -fexceptions -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Regression test: TryMarkNoThrow should see nounwind on plain
// direct wrappers when the callee was fully emitted before the wrapper
// started, matching LLVM CallBase::hasFnAttr behavior in OGCG.

int foo() { return 42; }

struct S {
  int x, y;
};

S bar() { return {1, 2}; }

void baz() {}

int wrap_scalar() { return foo(); }
S wrap_agg() { return bar(); }
void wrap_void() { baz(); }

// CIR: define dso_local noundef i32 @_Z11wrap_scalarv() #[[ATTR:[0-9]+]]
// CIR: define dso_local i64 @_Z8wrap_aggv() #[[ATTR]]
// CIR: define dso_local void @_Z9wrap_voidv() #[[ATTR]]
// CIR: attributes #[[ATTR]] = {{{.*}}nounwind{{.*}}}

// OGCG: define dso_local noundef i32 @_Z11wrap_scalarv() #[[ATTR:[0-9]+]]
// OGCG: define dso_local i64 @_Z8wrap_aggv() #[[ATTR]]
// OGCG: define dso_local void @_Z9wrap_voidv() #[[ATTR]]
// OGCG: attributes #[[ATTR]] = {{{.*}}nounwind{{.*}}}
