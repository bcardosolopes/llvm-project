// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

// Test ifunc definition support.

static int resolver_impl(int x) { return x + 1; }

typedef int (*fn_ptr)(int);
static fn_ptr my_resolver() { return resolver_impl; }

int ifunc_func(int x) __attribute__((ifunc("_ZL11my_resolverv")));

// CHECK: cir.alias external ifunc @_Z10ifunc_funci = @_ZL11my_resolverv
