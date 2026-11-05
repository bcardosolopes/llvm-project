// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Verify that __attribute__((ifunc)) with a static resolver works.
// The static resolver must be emitted even though nothing else uses it.

static void impl(void) {}
typedef void (*fn_t)(void);
static fn_t resolve(void) { return impl; }
void my_func(void) __attribute__((ifunc("resolve")));

// Verify that calling an ifunc works (the ifunc and call coexist in the TU).
// This used to crash with "redefinition of symbol named 'ifunc_callee'".

int ifunc_callee(void) __attribute__((ifunc("ifunc_callee_resolver")));
static void *ifunc_callee_resolver(void) { return 0; }
int call_ifunc(void) { return ifunc_callee(); }

// Both ifunc declarations appear at the top of the module.
// CIR-DAG: @my_func = ifunc void (), ptr @resolve
// CIR-DAG: @ifunc_callee = {{(dso_local )?}}ifunc i32 (), ptr @ifunc_callee_resolver

// CIR: define {{.*}} i32 @call_ifunc()
// CIR:   call i32 @ifunc_callee()
// CIR: define internal ptr @ifunc_callee_resolver()

// OGCG-DAG: @my_func = ifunc void (), ptr @resolve
// OGCG-DAG: @ifunc_callee = ifunc i32 (), ptr @ifunc_callee_resolver

// OGCG: define {{.*}} i32 @call_ifunc()
// OGCG:   %{{.*}} = call i32 @ifunc_callee()
// OGCG: define internal ptr @ifunc_callee_resolver()
