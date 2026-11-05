// REQUIRES: aarch64-registered-target

// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +mte -emit-llvm -fclangir %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s -check-prefix=CIR
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +mte -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s -check-prefix=OGCG

#include <stdint.h>
#include <stddef.h>

char *test_irg(char *p, uint64_t mask) {
  return __builtin_arm_irg(p, mask);
}

// CIR: define {{.*}}ptr @test_irg(ptr {{.*}}%{{.*}}, i64 {{.*}}%{{.*}})
// CIR:   %{{.*}} = call ptr @llvm.aarch64.irg(ptr %{{.*}}, i64 %{{.*}})

// OGCG: define {{.*}}ptr @test_irg(ptr {{.*}}%{{.*}}, i64 {{.*}}%{{.*}})
// OGCG:   %{{.*}} = call ptr @llvm.aarch64.irg(ptr %{{.*}}, i64 %{{.*}})

char *test_addg(char *p, unsigned offset) {
  return __builtin_arm_addg(p, 7);
}

// CIR: define {{.*}}ptr @test_addg(ptr {{.*}}%{{.*}}, i32 {{.*}}%{{.*}})
// CIR:   %{{.*}} = call ptr @llvm.aarch64.addg(ptr %{{.*}}, i64 {{.*}})

// OGCG: define {{.*}}ptr @test_addg(ptr {{.*}}%{{.*}}, i32 {{.*}}%{{.*}})
// OGCG:   %{{.*}} = call ptr @llvm.aarch64.addg(ptr %{{.*}}, i64 {{.*}})

uint64_t test_gmi(char *p, uint64_t mask) {
  return __builtin_arm_gmi(p, mask);
}

// CIR: define {{.*}}i64 @test_gmi(ptr {{.*}}%{{.*}}, i64 {{.*}}%{{.*}})
// CIR:   %{{.*}} = call i64 @llvm.aarch64.gmi(ptr %{{.*}}, i64 %{{.*}})

// OGCG: define {{.*}}i64 @test_gmi(ptr {{.*}}%{{.*}}, i64 {{.*}}%{{.*}})
// OGCG:   %{{.*}} = call i64 @llvm.aarch64.gmi(ptr %{{.*}}, i64 %{{.*}})

char *test_ldg(char *p) {
  return __builtin_arm_ldg(p);
}

// CIR: define {{.*}}ptr @test_ldg(ptr {{.*}}%{{.*}})
// CIR:   %{{.*}} = call ptr @llvm.aarch64.ldg(ptr %{{.*}}, ptr %{{.*}})

// OGCG: define {{.*}}ptr @test_ldg(ptr {{.*}}%{{.*}})
// OGCG:   %{{.*}} = call ptr @llvm.aarch64.ldg(ptr %{{.*}}, ptr %{{.*}})

void test_stg(char *p) {
  __builtin_arm_stg(p);
}

// CIR: define {{.*}}void @test_stg(ptr {{.*}}%{{.*}})
// CIR:   call void @llvm.aarch64.stg(ptr %{{.*}}, ptr %{{.*}})

// OGCG: define {{.*}}void @test_stg(ptr {{.*}}%{{.*}})
// OGCG:   call void @llvm.aarch64.stg(ptr %{{.*}}, ptr %{{.*}})

ptrdiff_t test_subp(char *a, char *b) {
  return __builtin_arm_subp(a, b);
}

// CIR: define {{.*}}i64 @test_subp(ptr {{.*}}%{{.*}}, ptr {{.*}}%{{.*}})
// CIR:   %{{.*}} = call i64 @llvm.aarch64.subp(ptr %{{.*}}, ptr %{{.*}})

// OGCG: define {{.*}}i64 @test_subp(ptr {{.*}}%{{.*}}, ptr {{.*}}%{{.*}})
// OGCG:   %{{.*}} = call i64 @llvm.aarch64.subp(ptr %{{.*}}, ptr %{{.*}})
