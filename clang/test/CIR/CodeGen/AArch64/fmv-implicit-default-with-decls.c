// RUN: %clang_cc1 -triple aarch64-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple aarch64-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Verify that a function with a plain definition (implicit default) followed
// by version declarations creates ifunc + resolver, matching OGCG.

int foo(void) { return 0; }
__attribute__((target_clones("dotprod", "lse"))) int foo(void);
__attribute__((target_version("jscvt"))) int foo(void);

int bar(void) { return foo(); }

// Both should create ifunc + resolver:
// CIR: @foo = weak_odr ifunc i32 (), ptr @foo.resolver
// CIR: define dso_local i32 @foo.default()
// CIR: define weak_odr ptr @foo.resolver()

// OGCG: @foo = weak_odr ifunc i32 (), ptr @foo.resolver
// OGCG: define dso_local i32 @foo.default()
// OGCG: define weak_odr ptr @foo.resolver()
