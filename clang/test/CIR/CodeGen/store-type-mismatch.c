// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -std=c89 -verify %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

// Test that CIR handles pointer type mismatches in store operations by
// inserting bitcasts.  CIR uses typed pointers, unlike LLVM IR's opaque
// pointers, so stores where the value's pointer type differs from the
// destination's pointee type require explicit bitcast reconciliation.

// K&R function pointer assignment: the no-proto declared type int(*)()
// differs from the actual definition int(*)(void*).
struct funcptr {
  int (*func)();
};

static int func(f) // expected-warning {{a function definition without a prototype is deprecated}}
  void *f;
{
  return 0;
}

void knr_funcptr() {
  struct funcptr fp;
  fp.func = &func;
  fp.func = func;
}

// CHECK: cir.func {{.*}}@knr_funcptr
// CHECK:   cir.get_global @func
// CHECK:   cir.cast bitcast
// CHECK:   cir.store
// CHECK:   cir.get_global @func
// CHECK:   cir.cast bitcast
// CHECK:   cir.store

// Void pointer stored to typed pointer destination through return.
void *void_ptr_return(char **x) {
  return x;
}
// CHECK: cir.func {{.*}}@void_ptr_return
