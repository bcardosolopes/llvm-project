// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir --check-prefix=CIR %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

// Test: __attribute__((malloc)) sets noalias on the return value.

__attribute__((malloc)) void *my_alloc(int size);

void *test(int n) {
  return my_alloc(n);
}

// CIR: cir.func {{.*}} @test
// CIR: cir.func private @my_alloc({{.*}}) -> (!cir.ptr<!void> {cir.noalias{{.*}}})

// LLVM-LABEL: define {{.*}} @test
// LLVM: call noalias {{.*}}ptr @my_alloc
// LLVM: declare noalias {{.*}}ptr @my_alloc

// OGCG: declare noalias {{.*}}ptr @my_alloc
