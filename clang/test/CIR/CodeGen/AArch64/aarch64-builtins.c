// REQUIRES: aarch64-registered-target

// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -emit-llvm -fclangir %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s -check-prefix=CIR
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s -check-prefix=OGCG

#include <stdint.h>

unsigned test_rbit(unsigned a) {
  return __builtin_arm_rbit(a);
}

// CIR: define {{.*}}i32 @test_rbit(i32 {{.*}}%[[A:.*]])
// CIR:   %{{.*}} = call i32 @llvm.bitreverse.i32(i32 %{{.*}})

// OGCG: define {{.*}}i32 @test_rbit(i32 {{.*}}%[[A:.*]])
// OGCG:   %{{.*}} = call i32 @llvm.bitreverse.i32(i32 %{{.*}})

uint64_t test_rbit64(uint64_t a) {
  return __builtin_arm_rbit64(a);
}

// CIR: define {{.*}}i64 @test_rbit64(i64 {{.*}}%[[A:.*]])
// CIR:   %{{.*}} = call i64 @llvm.bitreverse.i64(i64 %{{.*}})

// OGCG: define {{.*}}i64 @test_rbit64(i64 {{.*}}%[[A:.*]])
// OGCG:   %{{.*}} = call i64 @llvm.bitreverse.i64(i64 %{{.*}})

unsigned test_clz(unsigned a) {
  return __builtin_arm_clz(a);
}

// CIR: define {{.*}}i32 @test_clz(i32 {{.*}}%[[A:.*]])
// CIR:   %{{.*}} = call i32 @llvm.ctlz.i32(i32 %{{.*}}, i1 false)

// OGCG: define {{.*}}i32 @test_clz(i32 {{.*}}%[[A:.*]])
// OGCG:   %{{.*}} = call i32 @llvm.ctlz.i32(i32 %{{.*}}, i1 false)

unsigned test_clz64(uint64_t a) {
  return __builtin_arm_clz64(a);
}

// CIR: define {{.*}}i32 @test_clz64(i64 {{.*}}%[[A:.*]])
// CIR:   %[[CLZ:.*]] = call i64 @llvm.ctlz.i64(i64 %{{.*}}, i1 false)
// CIR:   %{{.*}} = trunc i64 %[[CLZ]] to i32

// OGCG: define {{.*}}i32 @test_clz64(i64 {{.*}}%[[A:.*]])
// OGCG:   %[[CLZ:.*]] = call i64 @llvm.ctlz.i64(i64 %{{.*}}, i1 false)
// OGCG:   %{{.*}} = trunc i64 %[[CLZ]] to i32

unsigned test_cls(uint32_t v) {
  return __builtin_arm_cls(v);
}

// CIR: define {{.*}}i32 @test_cls(i32 {{.*}}%[[V:.*]])
// CIR:   %{{.*}} = call i32 @llvm.aarch64.cls(i32 %{{.*}})

// OGCG: define {{.*}}i32 @test_cls(i32 {{.*}}%[[V:.*]])
// OGCG:   %{{.*}} = call i32 @llvm.aarch64.cls(i32 %{{.*}})

unsigned test_cls64(uint64_t v) {
  return __builtin_arm_cls64(v);
}

// CIR: define {{.*}}i32 @test_cls64(i64 {{.*}}%[[V:.*]])
// CIR:   %{{.*}} = call i32 @llvm.aarch64.cls64(i64 %{{.*}})

// OGCG: define {{.*}}i32 @test_cls64(i64 {{.*}}%[[V:.*]])
// OGCG:   %{{.*}} = call i32 @llvm.aarch64.cls64(i64 %{{.*}})

__attribute__((target("v8.5a")))
int32_t test_jcvt(double v) {
  return __builtin_arm_jcvt(v);
}

// CIR: define {{.*}}i32 @test_jcvt(double {{.*}}%[[V:.*]])
// CIR:   %{{.*}} = call i32 @llvm.aarch64.fjcvtzs(double %{{.*}})

// OGCG: define {{.*}}i32 @test_jcvt(double {{.*}}%[[V:.*]])
// OGCG:   %{{.*}} = call i32 @llvm.aarch64.fjcvtzs(double %{{.*}})

__attribute__((target("v8.5a")))
double test_rint32z(double a) {
  return __builtin_arm_rint32z(a);
}

// CIR: define {{.*}}double @test_rint32z(double {{.*}}%[[A:.*]])
// CIR:   %{{.*}} = call double @llvm.aarch64.frint32z.f64(double %{{.*}})

// OGCG: define {{.*}}double @test_rint32z(double {{.*}}%[[A:.*]])
// OGCG:   %{{.*}} = call double @llvm.aarch64.frint32z.f64(double %{{.*}})

__attribute__((target("v8.5a")))
float test_rint32zf(float a) {
  return __builtin_arm_rint32zf(a);
}

// CIR: define {{.*}}float @test_rint32zf(float {{.*}}%[[A:.*]])
// CIR:   %{{.*}} = call float @llvm.aarch64.frint32z.f32(float %{{.*}})

// OGCG: define {{.*}}float @test_rint32zf(float {{.*}}%[[A:.*]])
// OGCG:   %{{.*}} = call float @llvm.aarch64.frint32z.f32(float %{{.*}})

__attribute__((target("v8.5a")))
double test_rint64z(double a) {
  return __builtin_arm_rint64z(a);
}

// CIR: define {{.*}}double @test_rint64z(double {{.*}}%[[A:.*]])
// CIR:   %{{.*}} = call double @llvm.aarch64.frint64z.f64(double %{{.*}})

// OGCG: define {{.*}}double @test_rint64z(double {{.*}}%[[A:.*]])
// OGCG:   %{{.*}} = call double @llvm.aarch64.frint64z.f64(double %{{.*}})

__attribute__((target("v8.5a")))
float test_rint64zf(float a) {
  return __builtin_arm_rint64zf(a);
}

// CIR: define {{.*}}float @test_rint64zf(float {{.*}}%[[A:.*]])
// CIR:   %{{.*}} = call float @llvm.aarch64.frint64z.f32(float %{{.*}})

// OGCG: define {{.*}}float @test_rint64zf(float {{.*}}%[[A:.*]])
// OGCG:   %{{.*}} = call float @llvm.aarch64.frint64z.f32(float %{{.*}})

__attribute__((target("v8.5a")))
double test_rint32x(double a) {
  return __builtin_arm_rint32x(a);
}

// CIR: define {{.*}}double @test_rint32x(double {{.*}}%[[A:.*]])
// CIR:   %{{.*}} = call double @llvm.aarch64.frint32x.f64(double %{{.*}})

// OGCG: define {{.*}}double @test_rint32x(double {{.*}}%[[A:.*]])
// OGCG:   %{{.*}} = call double @llvm.aarch64.frint32x.f64(double %{{.*}})

__attribute__((target("v8.5a")))
float test_rint32xf(float a) {
  return __builtin_arm_rint32xf(a);
}

// CIR: define {{.*}}float @test_rint32xf(float {{.*}}%[[A:.*]])
// CIR:   %{{.*}} = call float @llvm.aarch64.frint32x.f32(float %{{.*}})

// OGCG: define {{.*}}float @test_rint32xf(float {{.*}}%[[A:.*]])
// OGCG:   %{{.*}} = call float @llvm.aarch64.frint32x.f32(float %{{.*}})

__attribute__((target("v8.5a")))
double test_rint64x(double a) {
  return __builtin_arm_rint64x(a);
}

// CIR: define {{.*}}double @test_rint64x(double {{.*}}%[[A:.*]])
// CIR:   %{{.*}} = call double @llvm.aarch64.frint64x.f64(double %{{.*}})

// OGCG: define {{.*}}double @test_rint64x(double {{.*}}%[[A:.*]])
// OGCG:   %{{.*}} = call double @llvm.aarch64.frint64x.f64(double %{{.*}})

__attribute__((target("v8.5a")))
float test_rint64xf(float a) {
  return __builtin_arm_rint64xf(a);
}

// CIR: define {{.*}}float @test_rint64xf(float {{.*}}%[[A:.*]])
// CIR:   %{{.*}} = call float @llvm.aarch64.frint64x.f32(float %{{.*}})

// OGCG: define {{.*}}float @test_rint64xf(float {{.*}}%[[A:.*]])
// OGCG:   %{{.*}} = call float @llvm.aarch64.frint64x.f32(float %{{.*}})

void test_trap() {
  __builtin_arm_trap(42);
}

// CIR: define {{.*}}void @test_trap()
// CIR:   call void @llvm.aarch64.break(i32 42)

// OGCG: define {{.*}}void @test_trap()
// OGCG:   call void @llvm.aarch64.break(i32 42)

void test_clrex(void) {
  __builtin_arm_clrex();
}

// CIR: define {{.*}}void @test_clrex()
// CIR:   call void @llvm.aarch64.clrex()

// OGCG: define {{.*}}void @test_clrex()
// OGCG:   call void @llvm.aarch64.clrex()

void *test_sponentry(void) {
  return __builtin_sponentry();
}

// CIR: define {{.*}}ptr @test_sponentry()
// CIR:   call ptr @llvm.sponentry.p0()

// OGCG: define {{.*}}ptr @test_sponentry()
// OGCG:   call ptr @llvm.sponentry.p0()

__attribute__((target("rand")))
int test_rndr(unsigned long *p) {
  return __builtin_arm_rndr(p);
}

// CIR: define {{.*}}i32 @test_rndr(ptr {{.*}}%{{.*}})
// CIR:   %{{.*}} = call { i64, i1 } @llvm.aarch64.rndr()
// CIR:   %{{.*}} = extractvalue { i64, i1 } %{{.*}}, 0
// CIR:   store i64 %{{.*}}, ptr %{{.*}}, align 8
// CIR:   %{{.*}} = extractvalue { i64, i1 } %{{.*}}, 1
// CIR:   %{{.*}} = zext i1 %{{.*}} to i32

// OGCG: define {{.*}}i32 @test_rndr(ptr {{.*}}%{{.*}})
// OGCG:   %{{.*}} = call { i64, i1 } @llvm.aarch64.rndr()
// OGCG:   %{{.*}} = extractvalue { i64, i1 } %{{.*}}, 0
// OGCG:   %{{.*}} = extractvalue { i64, i1 } %{{.*}}, 1
// OGCG:   store i64 %{{.*}}, ptr %{{.*}}, align 8
// OGCG:   %{{.*}} = zext i1 %{{.*}} to i32

__attribute__((target("rand")))
int test_rndrrs(unsigned long *p) {
  return __builtin_arm_rndrrs(p);
}

// CIR: define {{.*}}i32 @test_rndrrs(ptr {{.*}}%{{.*}})
// CIR:   %{{.*}} = call { i64, i1 } @llvm.aarch64.rndrrs()
// CIR:   %{{.*}} = extractvalue { i64, i1 } %{{.*}}, 0
// CIR:   store i64 %{{.*}}, ptr %{{.*}}, align 8
// CIR:   %{{.*}} = extractvalue { i64, i1 } %{{.*}}, 1
// CIR:   %{{.*}} = zext i1 %{{.*}} to i32

// OGCG: define {{.*}}i32 @test_rndrrs(ptr {{.*}}%{{.*}})
// OGCG:   %{{.*}} = call { i64, i1 } @llvm.aarch64.rndrrs()
// OGCG:   %{{.*}} = extractvalue { i64, i1 } %{{.*}}, 0
// OGCG:   %{{.*}} = extractvalue { i64, i1 } %{{.*}}, 1
// OGCG:   store i64 %{{.*}}, ptr %{{.*}}, align 8
// OGCG:   %{{.*}} = zext i1 %{{.*}} to i32

void test_clear_cache(char *begin, char *end) {
  __clear_cache(begin, end);
}

// CIR: declare {{.*}}void @__clear_cache(ptr{{.*}}, ptr{{.*}})
// CIR: define {{.*}}void @test_clear_cache(ptr {{.*}}%{{.*}}, ptr {{.*}}%{{.*}})
// CIR:   call void @__clear_cache(ptr{{.*}}%{{.*}}, ptr{{.*}}%{{.*}})

// OGCG: define {{.*}}void @test_clear_cache(ptr {{.*}}%{{.*}}, ptr {{.*}}%{{.*}})
// OGCG:   call void @__clear_cache(ptr {{.*}}%{{.*}}, ptr {{.*}}%{{.*}})

unsigned int test_rsr(void) {
  return __builtin_arm_rsr("1:2:3:4:5");
}

// CIR: define {{.*}}i32 @test_rsr()
// CIR:   %{{.*}} = call i64 @llvm.read_volatile_register.i64(metadata ![[SYSREG:.*]])
// CIR:   %{{.*}} = trunc i64 %{{.*}} to i32

// OGCG: define {{.*}}i32 @test_rsr()
// OGCG:   %{{.*}} = call i64 @llvm.read_volatile_register.i64(metadata ![[SYSREG:.*]])
// OGCG:   %{{.*}} = trunc i64 %{{.*}} to i32

unsigned long long test_rsr64(void) {
  return __builtin_arm_rsr64("1:2:3:4:5");
}

// CIR: define {{.*}}i64 @test_rsr64()
// CIR:   %{{.*}} = call i64 @llvm.read_volatile_register.i64(metadata ![[SYSREG]])

// OGCG: define {{.*}}i64 @test_rsr64()
// OGCG:   %{{.*}} = call i64 @llvm.read_volatile_register.i64(metadata ![[SYSREG]])

void *test_rsrp(void) {
  return __builtin_arm_rsrp("1:2:3:4:5");
}

// CIR: define {{.*}}ptr @test_rsrp()
// CIR:   %{{.*}} = call i64 @llvm.read_volatile_register.i64(metadata ![[SYSREG]])
// CIR:   %{{.*}} = inttoptr i64 %{{.*}} to ptr

// OGCG: define {{.*}}ptr @test_rsrp()
// OGCG:   %{{.*}} = call i64 @llvm.read_volatile_register.i64(metadata ![[SYSREG]])
// OGCG:   %{{.*}} = inttoptr i64 %{{.*}} to ptr

void test_wsr(unsigned int val) {
  __builtin_arm_wsr("1:2:3:4:5", val);
}

// CIR: define {{.*}}void @test_wsr(i32 {{.*}}%{{.*}})
// CIR:   %{{.*}} = zext i32 %{{.*}} to i64
// CIR:   call void @llvm.write_register.i64(metadata ![[SYSREG]], i64 %{{.*}})

// OGCG: define {{.*}}void @test_wsr(i32 {{.*}}%{{.*}})
// OGCG:   %{{.*}} = zext i32 %{{.*}} to i64
// OGCG:   call void @llvm.write_register.i64(metadata ![[SYSREG]], i64 %{{.*}})

void test_wsr64(unsigned long long val) {
  __builtin_arm_wsr64("1:2:3:4:5", val);
}

// CIR: define {{.*}}void @test_wsr64(i64 {{.*}}%{{.*}})
// CIR:   call void @llvm.write_register.i64(metadata ![[SYSREG]], i64 %{{.*}})

// OGCG: define {{.*}}void @test_wsr64(i64 {{.*}}%{{.*}})
// OGCG:   call void @llvm.write_register.i64(metadata ![[SYSREG]], i64 %{{.*}})

void test_wsrp(void *val) {
  __builtin_arm_wsrp("1:2:3:4:5", val);
}

// CIR: define {{.*}}void @test_wsrp(ptr {{.*}}%{{.*}})
// CIR:   %{{.*}} = ptrtoint ptr %{{.*}} to i64
// CIR:   call void @llvm.write_register.i64(metadata ![[SYSREG]], i64 %{{.*}})

// OGCG: define {{.*}}void @test_wsrp(ptr {{.*}}%{{.*}})
// OGCG:   %{{.*}} = ptrtoint ptr %{{.*}} to i64
// OGCG:   call void @llvm.write_register.i64(metadata ![[SYSREG]], i64 %{{.*}})

__attribute__((target("mte,mops")))
void *test_mops_memset_tag(void *dst, int val, unsigned long size) {
  return __builtin_arm_mops_memset_tag(dst, val, size);
}

// CIR: define {{.*}}ptr @test_mops_memset_tag(ptr {{.*}}%{{.*}}, i32 {{.*}}%{{.*}}, i64 {{.*}}%{{.*}})
// CIR:   %{{.*}} = trunc i32 %{{.*}} to i8
// CIR:   %{{.*}} = call ptr @llvm.aarch64.mops.memset.tag(ptr %{{.*}}, i8 %{{.*}}, i64 %{{.*}})

// OGCG: define {{.*}}ptr @test_mops_memset_tag(ptr {{.*}}%{{.*}}, i32 {{.*}}%{{.*}}, i64 {{.*}}%{{.*}})
// OGCG:   %{{.*}} = trunc i32 %{{.*}} to i8
// OGCG:   %{{.*}} = call ptr @llvm.aarch64.mops.memset.tag(ptr %{{.*}}, i8 %{{.*}}, i64 %{{.*}})

// CIR: ![[SYSREG]] = !{!"1:2:3:4:5"}
// OGCG: ![[SYSREG]] = !{!"1:2:3:4:5"}
