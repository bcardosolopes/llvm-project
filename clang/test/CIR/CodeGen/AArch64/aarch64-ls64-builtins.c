// REQUIRES: aarch64-registered-target

// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +ls64 -emit-llvm -fclangir %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s -check-prefix=CIR
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +ls64 -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s -check-prefix=OGCG

void test_ld64b(const void *addr, unsigned long *val) {
  __builtin_arm_ld64b(addr, val);
}

// CIR: define {{.*}}void @test_ld64b(ptr {{.*}}%{{.*}}, ptr {{.*}}%{{.*}})
// CIR:   %[[RESULT:.*]] = call { i64, i64, i64, i64, i64, i64, i64, i64 } @llvm.aarch64.ld64b(ptr %{{.*}})
// CIR:   %{{.*}} = extractvalue { i64, i64, i64, i64, i64, i64, i64, i64 } %[[RESULT]], 0
// CIR:   store i64 %{{.*}}, ptr %{{.*}}, align 8
// CIR:   %{{.*}} = extractvalue { i64, i64, i64, i64, i64, i64, i64, i64 } %[[RESULT]], 7
// CIR:   store i64 %{{.*}}, ptr %{{.*}}, align 8

// OGCG: define {{.*}}void @test_ld64b(ptr {{.*}}%{{.*}}, ptr {{.*}}%{{.*}})
// OGCG:   %{{.*}} = call { i64, i64, i64, i64, i64, i64, i64, i64 } @llvm.aarch64.ld64b(ptr %{{.*}})
// OGCG:   %{{.*}} = extractvalue { i64, i64, i64, i64, i64, i64, i64, i64 } %{{.*}}, 0
// OGCG:   store i64 %{{.*}}, ptr %{{.*}}, align 8

void test_st64b(void *addr, const unsigned long *val) {
  __builtin_arm_st64b(addr, val);
}

// CIR: define {{.*}}void @test_st64b(ptr {{.*}}%{{.*}}, ptr {{.*}}%{{.*}})
// CIR:   %{{.*}} = load i64, ptr %{{.*}}, align 8
// CIR:   call void @llvm.aarch64.st64b(ptr %{{.*}}, i64 %{{.*}}, i64 %{{.*}}, i64 %{{.*}}, i64 %{{.*}}, i64 %{{.*}}, i64 %{{.*}}, i64 %{{.*}}, i64 %{{.*}})

// OGCG: define {{.*}}void @test_st64b(ptr {{.*}}%{{.*}}, ptr {{.*}}%{{.*}})
// OGCG:   call void @llvm.aarch64.st64b(ptr %{{.*}}, i64 %{{.*}}, i64 %{{.*}}, i64 %{{.*}}, i64 %{{.*}}, i64 %{{.*}}, i64 %{{.*}}, i64 %{{.*}}, i64 %{{.*}})

unsigned long test_st64bv(void *addr, const unsigned long *val) {
  return __builtin_arm_st64bv(addr, val);
}

// CIR: define {{.*}}i64 @test_st64bv(ptr {{.*}}%{{.*}}, ptr {{.*}}%{{.*}})
// CIR:   %{{.*}} = call i64 @llvm.aarch64.st64bv(ptr %{{.*}}, i64 %{{.*}}, i64 %{{.*}}, i64 %{{.*}}, i64 %{{.*}}, i64 %{{.*}}, i64 %{{.*}}, i64 %{{.*}}, i64 %{{.*}})

// OGCG: define {{.*}}i64 @test_st64bv(ptr {{.*}}%{{.*}}, ptr {{.*}}%{{.*}})
// OGCG:   %{{.*}} = call i64 @llvm.aarch64.st64bv(ptr %{{.*}}, i64 %{{.*}}, i64 %{{.*}}, i64 %{{.*}}, i64 %{{.*}}, i64 %{{.*}}, i64 %{{.*}}, i64 %{{.*}}, i64 %{{.*}})

unsigned long test_st64bv0(void *addr, const unsigned long *val) {
  return __builtin_arm_st64bv0(addr, val);
}

// CIR: define {{.*}}i64 @test_st64bv0(ptr {{.*}}%{{.*}}, ptr {{.*}}%{{.*}})
// CIR:   %{{.*}} = call i64 @llvm.aarch64.st64bv0(ptr %{{.*}}, i64 %{{.*}}, i64 %{{.*}}, i64 %{{.*}}, i64 %{{.*}}, i64 %{{.*}}, i64 %{{.*}}, i64 %{{.*}}, i64 %{{.*}})

// OGCG: define {{.*}}i64 @test_st64bv0(ptr {{.*}}%{{.*}}, ptr {{.*}}%{{.*}})
// OGCG:   %{{.*}} = call i64 @llvm.aarch64.st64bv0(ptr %{{.*}}, i64 %{{.*}}, i64 %{{.*}}, i64 %{{.*}}, i64 %{{.*}}, i64 %{{.*}}, i64 %{{.*}}, i64 %{{.*}}, i64 %{{.*}})
