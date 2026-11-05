// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -target-feature +prefetchi \
// RUN:   -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -target-feature +prefetchi \
// RUN:   -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// prefetchi maps to llvm.prefetch with rw=0 and cache_type=0
void test_prefetchi(const void *p) {
  __builtin_ia32_prefetchi(p, 3);
}
// CHECK-LABEL: @test_prefetchi
// CHECK: call void @llvm.prefetch.p0(ptr %{{.*}}, i32 0, i32 3, i32 0)
// OGCG-LABEL: @test_prefetchi
// OGCG: call void @llvm.prefetch.p0(ptr %{{.*}}, i32 0, i32 3, i32 0)

void test_prefetchi_locality2(const void *p) {
  __builtin_ia32_prefetchi(p, 2);
}
// CHECK-LABEL: @test_prefetchi_locality2
// CHECK: call void @llvm.prefetch.p0(ptr %{{.*}}, i32 0, i32 2, i32 0)
// OGCG-LABEL: @test_prefetchi_locality2
// OGCG: call void @llvm.prefetch.p0(ptr %{{.*}}, i32 0, i32 2, i32 0)
