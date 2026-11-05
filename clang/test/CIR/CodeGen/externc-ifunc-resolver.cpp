// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -o %t.cir %s
// RUN: FileCheck --input-file=%t.cir --check-prefix=CIR %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -o %t.ll %s
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -o %t.ogcg.ll %s
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

// Test that ifunc resolvers inside extern "C" blocks are correctly resolved
// to their mangled names. Static functions inside extern "C" still get C++
// mangling, and the ifunc must point to the definition, not a stray
// declaration under the unmangled name.

extern "C" {
__attribute__((used)) static void *resolve_foo() { return 0; }
__attribute__((ifunc("resolve_foo"))) char *foo();
__attribute__((ifunc("resolve_foo"))) void foo2(int);
__attribute__((ifunc("resolve_foo"))) char foo3(float);
__attribute__((ifunc("resolve_foo"))) char foo4(float);
}

// CIR: cir.alias external ifunc @foo = @_ZL11resolve_foov
// CIR: cir.alias external ifunc @foo2 = @_ZL11resolve_foov
// CIR: cir.alias external ifunc @foo3 = @_ZL11resolve_foov
// CIR: cir.alias external ifunc @foo4 = @_ZL11resolve_foov
// CIR: cir.alias internal @resolve_foo = @_ZL11resolve_foov
// CIR: cir.func {{.*}} @_ZL11resolve_foov()

// LLVM: @resolve_foo = internal alias ptr (), ptr @_ZL11resolve_foov
// LLVM: @foo = ifunc ptr (), ptr @_ZL11resolve_foov
// LLVM: @foo2 = ifunc void (i32), ptr @_ZL11resolve_foov
// LLVM: @foo3 = ifunc i8 (float), ptr @_ZL11resolve_foov
// LLVM: @foo4 = ifunc i8 (float), ptr @_ZL11resolve_foov
// LLVM: define internal noundef ptr @_ZL11resolve_foov()

// OGCG: @resolve_foo = internal alias ptr (), ptr @_ZL11resolve_foov
// OGCG: @foo = ifunc ptr (), ptr @_ZL11resolve_foov
// OGCG: @foo2 = ifunc void (i32), ptr @_ZL11resolve_foov
// OGCG: @foo3 = ifunc i8 (float), ptr @_ZL11resolve_foov
// OGCG: @foo4 = ifunc i8 (float), ptr @_ZL11resolve_foov
// OGCG: define internal noundef ptr @_ZL11resolve_foov()
