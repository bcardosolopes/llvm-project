// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -Wno-implicit-function-declaration -w \
// RUN:   -fclangir -emit-llvm -disable-llvm-passes -fmath-errno %s -o %t.cir-errno.ll
// RUN: FileCheck --check-prefix=CIR-ERRNO --input-file=%t.cir-errno.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -Wno-implicit-function-declaration -w \
// RUN:   -emit-llvm -disable-llvm-passes -fmath-errno %s -o %t.ogcg-errno.ll
// RUN: FileCheck --check-prefix=OGCG-ERRNO --input-file=%t.ogcg-errno.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -Wno-implicit-function-declaration -w \
// RUN:   -fclangir -emit-llvm -disable-llvm-passes -ffp-exception-behavior=strict %s \
// RUN:   -o %t.cir-strict.ll
// RUN: FileCheck --check-prefix=CIR-STRICT --input-file=%t.cir-strict.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -Wno-implicit-function-declaration -w \
// RUN:   -emit-llvm -disable-llvm-passes -ffp-exception-behavior=strict %s \
// RUN:   -o %t.ogcg-strict.ll
// RUN: FileCheck --check-prefix=OGCG-STRICT --input-file=%t.ogcg-strict.ll %s

void test_sincos_alias(double x, double *sinp, double *cosp) {
  sincos(x, sinp, cosp);
}

void test_sincosf_alias(float x, float *sinp, float *cosp) {
  sincosf(x, sinp, cosp);
}

void test_sincosl_alias(long double x, long double *sinp, long double *cosp) {
  sincosl(x, sinp, cosp);
}

// CIR-ERRNO-LABEL: define {{.*}} @test_sincos_alias(
// CIR-ERRNO-NOT: @llvm.sincos
// CIR-ERRNO: call void @sincos(double noundef
// CIR-ERRNO: ret void
// CIR-ERRNO-LABEL: define {{.*}} @test_sincosf_alias(
// CIR-ERRNO-NOT: @llvm.sincos
// CIR-ERRNO: call void @sincosf(float noundef
// CIR-ERRNO: ret void
// CIR-ERRNO-LABEL: define {{.*}} @test_sincosl_alias(
// CIR-ERRNO-NOT: @llvm.sincos
// CIR-ERRNO: call void @sincosl(x86_fp80 noundef
// CIR-ERRNO: ret void

// OGCG-ERRNO-LABEL: define {{.*}} @test_sincos_alias(
// OGCG-ERRNO-NOT: @llvm.sincos
// OGCG-ERRNO: call void @sincos(double noundef
// OGCG-ERRNO: ret void
// OGCG-ERRNO-LABEL: define {{.*}} @test_sincosf_alias(
// OGCG-ERRNO-NOT: @llvm.sincos
// OGCG-ERRNO: call void @sincosf(float noundef
// OGCG-ERRNO: ret void
// OGCG-ERRNO-LABEL: define {{.*}} @test_sincosl_alias(
// OGCG-ERRNO-NOT: @llvm.sincos
// OGCG-ERRNO: call void @sincosl(x86_fp80 noundef
// OGCG-ERRNO: ret void

// CIR-STRICT-LABEL: define {{.*}} @test_sincos_alias(
// CIR-STRICT-NOT: @llvm.sincos
// CIR-STRICT: call void @sincos(double noundef
// CIR-STRICT: ret void
// CIR-STRICT-LABEL: define {{.*}} @test_sincosf_alias(
// CIR-STRICT-NOT: @llvm.sincos
// CIR-STRICT: call void @sincosf(float noundef
// CIR-STRICT: ret void
// CIR-STRICT-LABEL: define {{.*}} @test_sincosl_alias(
// CIR-STRICT-NOT: @llvm.sincos
// CIR-STRICT: call void @sincosl(x86_fp80 noundef
// CIR-STRICT: ret void

// OGCG-STRICT-LABEL: define {{.*}} @test_sincos_alias(
// OGCG-STRICT-NOT: @llvm.sincos
// OGCG-STRICT: call void @sincos(double noundef
// OGCG-STRICT: ret void
// OGCG-STRICT-LABEL: define {{.*}} @test_sincosf_alias(
// OGCG-STRICT-NOT: @llvm.sincos
// OGCG-STRICT: call void @sincosf(float noundef
// OGCG-STRICT: ret void
// OGCG-STRICT-LABEL: define {{.*}} @test_sincosl_alias(
// OGCG-STRICT-NOT: @llvm.sincos
// OGCG-STRICT: call void @sincosl(x86_fp80 noundef
// OGCG-STRICT: ret void
