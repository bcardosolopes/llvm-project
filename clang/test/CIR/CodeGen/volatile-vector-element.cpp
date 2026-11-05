// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM

// Test that volatile vector element load/store passes the volatile flag
// through to CIR load/store operations.

typedef __attribute__(( ext_vector_type(4) )) int extv4;
volatile extv4 vVE;

// CHECK: cir.func {{.*}} @_Z29test_volatile_ext_vector_loadv
// LLVM: define {{.*}} @_Z29test_volatile_ext_vector_loadv
int test_volatile_ext_vector_load() {
  // The ext vector element load should be volatile
  // CHECK: cir.load volatile
  // LLVM: load volatile <4 x i32>
  return vVE.x;
}

typedef int v4si __attribute__((vector_size(16)));
volatile v4si vV;

// CHECK: cir.func {{.*}} @_Z30test_volatile_vector_elt_storei
// LLVM: define {{.*}} @_Z30test_volatile_vector_elt_storei
void test_volatile_vector_elt_store(int val) {
  // The vector element store should do volatile read-modify-write
  // CHECK: cir.load volatile
  // CHECK: cir.store volatile
  // LLVM: load volatile <4 x i32>
  // LLVM: store volatile <4 x i32>
  vV[0] = val;
}
