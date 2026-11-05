// REQUIRES: aarch64-registered-target

// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -fms-extensions -emit-llvm -fclangir %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s -check-prefix=CIR
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -fms-extensions -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s -check-prefix=OGCG

void __prefetch(const void *);

void test_prefetch_consts(void *p) {
  __prefetch(p);
}

// CIR-LABEL: define {{.*}}void @test_prefetch_consts(ptr {{.*}}%{{.*}})
// CIR: call void @llvm.prefetch.p0(ptr %{{.*}}, i32 0, i32 3, i32 1)

// OGCG-LABEL: define {{.*}}void @test_prefetch_consts(ptr {{.*}}%{{.*}})
// OGCG: call void @llvm.prefetch.p0(ptr %{{.*}}, i32 0, i32 3, i32 1)
