// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG
// REQUIRES: x86-registered-target
//
// Test atomic write of non-scalar types (complex). The omp.atomic.write op
// only supports scalar types, so complex writes go through emitAtomicStore
// which uses __atomic_store libcalls or integer-casting paths.

_Complex float cfv, cfx;

void test_complex_float_write(void) {
#pragma omp atomic write
  cfx = cfv;
}

// CIR-LABEL: cir.func {{.*}} @test_complex_float_write()
// CIR:         cir.call @__atomic_store(

// LLVM-LABEL: define {{.*}} @test_complex_float_write()
// LLVM:         call void @__atomic_store(i64 noundef 8, ptr noundef @cfx,
// LLVM-SAME:      ptr noundef %{{.*}}, i32 noundef 0)

// OGCG-LABEL: define {{.*}} @test_complex_float_write()
// OGCG:         call void @__atomic_store(i64 noundef 8, ptr noundef @cfx,
// OGCG-SAME:      ptr noundef %{{.*}}, i32 noundef 0)

_Complex int civ, cix;

void test_complex_int_write(void) {
#pragma omp atomic write
  cix = civ;
}

// CIR-LABEL: cir.func {{.*}} @test_complex_int_write()
// CIR:         cir.call @__atomic_store(

// LLVM-LABEL: define {{.*}} @test_complex_int_write()
// LLVM:         call void @__atomic_store(i64 noundef 8, ptr noundef @cix,
// LLVM-SAME:      ptr noundef %{{.*}}, i32 noundef 0)

// OGCG-LABEL: define {{.*}} @test_complex_int_write()
// OGCG:         call void @__atomic_store(i64 noundef 8, ptr noundef @cix,
// OGCG-SAME:      ptr noundef %{{.*}}, i32 noundef 0)
