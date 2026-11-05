// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

// Verify that __attribute__((malloc)) adds noalias on the return value.
// The getDeallocator() null check ensures noalias is only added
// when no deallocator is specified. Since Clang doesn't yet support the
// malloc(deallocator) syntax, getDeallocator() is always null for now.

__attribute__((malloc)) void *my_malloc(int size);

void test(void) {
  void *p = my_malloc(42);
}

// CHECK-DAG: cir.func private @my_malloc({{.*}}) -> (!cir.ptr<!void> {cir.noalias
