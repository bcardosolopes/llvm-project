// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t-ogcg.ll
// RUN: FileCheck --input-file=%t-ogcg.ll %s --check-prefix=OGCG

// Previously crashed with "emitLoadOfScalar: load atomic" NYI.

_Atomic int x;

int test_atomic_load() {
    return x;
}

// CIR: cir.func {{.*}}@test_atomic_load()
// CIR:   cir.load {{.*}} atomic(seq_cst)

// LLVM: define {{.*}}@test_atomic_load()
// LLVM:   load atomic i32, ptr @x seq_cst

// OGCG: define {{.*}}@test_atomic_load()
// OGCG:   load atomic i32, ptr @x seq_cst

_Atomic int y;

void test_atomic_store(int val) {
    y = val;
}

// CIR: cir.func {{.*}}@test_atomic_store(
// CIR:   cir.store {{.*}} atomic(seq_cst)

// LLVM: define {{.*}}@test_atomic_store(
// LLVM:   store atomic i32 %{{.*}}, ptr @y seq_cst

// OGCG: define {{.*}}@test_atomic_store(
// OGCG:   store atomic i32 %{{.*}}, ptr @y seq_cst
