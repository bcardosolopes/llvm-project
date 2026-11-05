// RUN: not %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o /dev/null 2>&1 | FileCheck %s --check-prefix=ERR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -DVALID %s -o - | FileCheck %s --check-prefix=VALID

#ifndef VALID
static void *resolve_foo() { return 0; }
__attribute__((ifunc("resolve_foo"))) void *foo();
__attribute__((ifunc("missing"))) void *bar();

// ERR: error: ifunc must point to a defined function
// ERR: note: the function specified in an ifunc must refer to its mangled name
// ERR: note: function by that name is mangled as "_ZL11resolve_foov"
// ERR: error: ifunc must point to a defined function
// ERR: note: the function specified in an ifunc must refer to its mangled name
#else
static void *resolve_foo() { return 0; }
__attribute__((ifunc("_ZL11resolve_foov"))) void *foo();

// VALID: @_Z3foov = ifunc ptr (), ptr @_ZL11resolve_foov
// VALID: define internal{{.*}} ptr @_ZL11resolve_foov()
#endif
