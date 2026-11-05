// REQUIRES: aarch64-registered-target
//
// RUN: %clang_cc1 -O3 -triple aarch64-linux-gnu -target-feature +sve -target-feature +sve2p1 \
// RUN:   -mvscale-min=1 -mvscale-max=1 -disable-llvm-passes -fclangir -emit-llvm \
// RUN:   -internal-isystem %resource_dir/include -o %t.cir.ll %s
// RUN: FileCheck --check-prefix=CHECK --input-file=%t.cir.ll %s
// RUN: %clang_cc1 -O3 -triple aarch64-linux-gnu -target-feature +sve -target-feature +sve2p1 \
// RUN:   -mvscale-min=1 -mvscale-max=1 -disable-llvm-passes -emit-llvm \
// RUN:   -internal-isystem %resource_dir/include -o %t.ogcg.ll %s
// RUN: FileCheck --check-prefix=CHECK --input-file=%t.ogcg.ll %s

#include <arm_sve.h>
#include <stdarg.h>

typedef svfloat32_t fvec32 __attribute__((arm_sve_vector_bits(128)));
typedef svfloat64_t fvec64 __attribute__((arm_sve_vector_bits(128)));
typedef svbool_t bvec __attribute__((arm_sve_vector_bits(128)));
typedef svmfloat8_t mfvec8 __attribute__((arm_sve_vector_bits(128)));

typedef struct {
  bvec a;
  fvec64 x;
  fvec32 y[2];
  mfvec8 z;
  bvec b;
} PST;

void use1(bvec, fvec32);

void test_va_arg(int n, ...) {
  va_list ap;
  va_start(ap, n);
  PST v = va_arg(ap, PST);
  va_end(ap);
  use1(v.a, v.y[1]);
}

// CHECK-LABEL: define dso_local void @test_va_arg(i32 noundef %{{.*}}, ...)
// CHECK: call void @llvm.memcpy.p0.p0.i64(ptr align 16 %{{.*}}, ptr align 16 %{{.*}}, i64 96, i1 false), !tbaa.struct ![[PST_TBAA:[0-9]+]]
// CHECK: ![[PST_TBAA]] = !{i64 0, i64 2, ![[CHAR_ACCESS:[0-9]+]], i64 16, i64 16, ![[CHAR_ACCESS]], i64 32, i64 32, ![[CHAR_ACCESS]], i64 64, i64 16, ![[CHAR_ACCESS]], i64 80, i64 2, ![[CHAR_ACCESS]]}
