// RUN: %clang_cc1 -std=c17 -triple x86_64-unknown-linux-gnu -fclangir \
// RUN:     -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -std=c17 -triple x86_64-unknown-linux-gnu -fclangir \
// RUN:     -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s
// RUN: %clang_cc1 -std=c17 -triple x86_64-unknown-linux-gnu \
// RUN:     -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s
extern void sincos(double, double *, double *);
extern void sincosf(float, float *, float *);
extern void sincosl(long double, long double *, long double *);
extern void sincosf128(__float128, __float128 *, __float128 *);

// sincos — float

void test_sincosf(float x, float *sinp, float *cosp) {
  __builtin_sincosf(x, sinp, cosp);
}

// CIR-LABEL: @test_sincosf
// CIR:   cir.call @__builtin_sincosf({{.*}}) : (!cir.float, !cir.ptr<!cir.float>, !cir.ptr<!cir.float>) -> ()

// CHECK-LABEL: define {{.*}} @test_sincosf(
// CHECK:   %[[CALL:.*]] = call { float, float } @llvm.sincos.f32(float %{{.*}})
// CHECK:   %[[SIN:.*]] = extractvalue { float, float } %[[CALL]], 0
// CHECK:   %[[COS:.*]] = extractvalue { float, float } %[[CALL]], 1
// CHECK:   store float %[[SIN]], ptr %{{.*}}
// CHECK:   store float %[[COS]], ptr %{{.*}}
// CHECK:   ret void

// OGCG-LABEL: define {{.*}} @test_sincosf(
// OGCG:   %[[CALL:.*]] = call { float, float } @llvm.sincos.f32(float %{{.*}})
// OGCG:   %[[SIN:.*]] = extractvalue { float, float } %[[CALL]], 0
// OGCG:   %[[COS:.*]] = extractvalue { float, float } %[[CALL]], 1
// OGCG:   store float %[[SIN]], ptr %{{.*}}
// OGCG:   store float %[[COS]], ptr %{{.*}}
// OGCG:   ret void

// sincos — double

void test_sincos(double x, double *sinp, double *cosp) {
  __builtin_sincos(x, sinp, cosp);
}

// CIR-LABEL: @test_sincos
// CIR:   cir.call @__builtin_sincos({{.*}}) : (!cir.double, !cir.ptr<!cir.double>, !cir.ptr<!cir.double>) -> ()

// CHECK-LABEL: define {{.*}} @test_sincos(
// CHECK:   %[[CALL:.*]] = call { double, double } @llvm.sincos.f64(double %{{.*}})
// CHECK:   %[[SIN:.*]] = extractvalue { double, double } %[[CALL]], 0
// CHECK:   %[[COS:.*]] = extractvalue { double, double } %[[CALL]], 1
// CHECK:   store double %[[SIN]], ptr %{{.*}}
// CHECK:   store double %[[COS]], ptr %{{.*}}
// CHECK:   ret void

// OGCG-LABEL: define {{.*}} @test_sincos(
// OGCG:   %[[CALL:.*]] = call { double, double } @llvm.sincos.f64(double %{{.*}})
// OGCG:   %[[SIN:.*]] = extractvalue { double, double } %[[CALL]], 0
// OGCG:   %[[COS:.*]] = extractvalue { double, double } %[[CALL]], 1
// OGCG:   store double %[[SIN]], ptr %{{.*}}
// OGCG:   store double %[[COS]], ptr %{{.*}}
// OGCG:   ret void

void test_sincos_alias(double x, double *sinp, double *cosp) {
  sincos(x, sinp, cosp);
}

// CIR-LABEL: @test_sincos_alias
// CIR:   cir.call @sincos({{.*}}) : (!cir.double, !cir.ptr<!cir.double>, !cir.ptr<!cir.double>) -> ()

// CHECK-LABEL: define {{.*}} @test_sincos_alias(
// CHECK:   call void @sincos(double noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}})
// CHECK:   ret void

// OGCG-LABEL: define {{.*}} @test_sincos_alias(
// OGCG:   call void @sincos(double noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}})
// OGCG:   ret void

void test_sincosf_alias(float x, float *sinp, float *cosp) {
  sincosf(x, sinp, cosp);
}

// CIR-LABEL: @test_sincosf_alias
// CIR:   cir.call @sincosf({{.*}}) : (!cir.float, !cir.ptr<!cir.float>, !cir.ptr<!cir.float>) -> ()

// CHECK-LABEL: define {{.*}} @test_sincosf_alias(
// CHECK:   call void @sincosf(float noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}})
// CHECK:   ret void

// OGCG-LABEL: define {{.*}} @test_sincosf_alias(
// OGCG:   call void @sincosf(float noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}})
// OGCG:   ret void

void test_sincosl(long double x, long double *sinp, long double *cosp) {
  __builtin_sincosl(x, sinp, cosp);
}

// CIR-LABEL: @test_sincosl
// CIR:   cir.call @__builtin_sincosl({{.*}}) : (!cir.long_double<!cir.f80>, !cir.ptr<!cir.long_double<!cir.f80>>, !cir.ptr<!cir.long_double<!cir.f80>>) -> ()

// CHECK-LABEL: define {{.*}} @test_sincosl(
// CHECK:   %[[CALL:.*]] = call { x86_fp80, x86_fp80 } @llvm.sincos.f80(x86_fp80 %{{.*}})
// CHECK:   %[[SIN:.*]] = extractvalue { x86_fp80, x86_fp80 } %[[CALL]], 0
// CHECK:   %[[COS:.*]] = extractvalue { x86_fp80, x86_fp80 } %[[CALL]], 1
// CHECK:   store x86_fp80 %[[SIN]], ptr %{{.*}}
// CHECK:   store x86_fp80 %[[COS]], ptr %{{.*}}
// CHECK:   ret void

// OGCG-LABEL: define {{.*}} @test_sincosl(
// OGCG:   %[[CALL:.*]] = call { x86_fp80, x86_fp80 } @llvm.sincos.f80(x86_fp80 %{{.*}})
// OGCG:   %[[SIN:.*]] = extractvalue { x86_fp80, x86_fp80 } %[[CALL]], 0
// OGCG:   %[[COS:.*]] = extractvalue { x86_fp80, x86_fp80 } %[[CALL]], 1
// OGCG:   store x86_fp80 %[[SIN]], ptr %{{.*}}
// OGCG:   store x86_fp80 %[[COS]], ptr %{{.*}}
// OGCG:   ret void

void test_sincosf128(__float128 x, __float128 *sinp, __float128 *cosp) {
  __builtin_sincosf128(x, sinp, cosp);
}

// CIR-LABEL: @test_sincosf128
// CIR:   cir.call @__builtin_sincosf128({{.*}}) : (!cir.f128, !cir.ptr<!cir.f128>, !cir.ptr<!cir.f128>) -> ()

// CHECK-LABEL: define {{.*}} @test_sincosf128(
// CHECK:   %[[CALL:.*]] = call { fp128, fp128 } @llvm.sincos.f128(fp128 %{{.*}})
// CHECK:   %[[SIN:.*]] = extractvalue { fp128, fp128 } %[[CALL]], 0
// CHECK:   %[[COS:.*]] = extractvalue { fp128, fp128 } %[[CALL]], 1
// CHECK:   store fp128 %[[SIN]], ptr %{{.*}}
// CHECK:   store fp128 %[[COS]], ptr %{{.*}}
// CHECK:   ret void

// OGCG-LABEL: define {{.*}} @test_sincosf128(
// OGCG:   %[[CALL:.*]] = call { fp128, fp128 } @llvm.sincos.f128(fp128 %{{.*}})
// OGCG:   %[[SIN:.*]] = extractvalue { fp128, fp128 } %[[CALL]], 0
// OGCG:   %[[COS:.*]] = extractvalue { fp128, fp128 } %[[CALL]], 1
// OGCG:   store fp128 %[[SIN]], ptr %{{.*}}
// OGCG:   store fp128 %[[COS]], ptr %{{.*}}
// OGCG:   ret void

void test_sincosl_alias(long double x, long double *sinp, long double *cosp) {
  sincosl(x, sinp, cosp);
}

// CIR-LABEL: @test_sincosl_alias
// CIR:   cir.call @sincosl({{.*}}) : (!cir.long_double<!cir.f80>, !cir.ptr<!cir.long_double<!cir.f80>>, !cir.ptr<!cir.long_double<!cir.f80>>) -> ()

// CHECK-LABEL: define {{.*}} @test_sincosl_alias(
// CHECK:   call void @sincosl(x86_fp80 noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}})
// CHECK:   ret void

// OGCG-LABEL: define {{.*}} @test_sincosl_alias(
// OGCG:   call void @sincosl(x86_fp80 noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}})
// OGCG:   ret void

// sincospi — float

void test_sincospif(float x, float *sinp, float *cosp) {
  __builtin_sincospif(x, sinp, cosp);
}

// CIR-LABEL: @test_sincospif
// CIR:   cir.call @__builtin_sincospif({{.*}}) : (!cir.float, !cir.ptr<!cir.float>, !cir.ptr<!cir.float>) -> ()

// CHECK-LABEL: define {{.*}} @test_sincospif(
// CHECK:   %[[CALL:.*]] = call { float, float } @llvm.sincospi.f32(float %{{.*}})
// CHECK:   %[[SIN:.*]] = extractvalue { float, float } %[[CALL]], 0
// CHECK:   %[[COS:.*]] = extractvalue { float, float } %[[CALL]], 1
// CHECK:   store float %[[SIN]], ptr %{{.*}}
// CHECK:   store float %[[COS]], ptr %{{.*}}
// CHECK:   ret void

// OGCG-LABEL: define {{.*}} @test_sincospif(
// OGCG:   %[[CALL:.*]] = call { float, float } @llvm.sincospi.f32(float %{{.*}})
// OGCG:   %[[SIN:.*]] = extractvalue { float, float } %[[CALL]], 0
// OGCG:   %[[COS:.*]] = extractvalue { float, float } %[[CALL]], 1
// OGCG:   store float %[[SIN]], ptr %{{.*}}
// OGCG:   store float %[[COS]], ptr %{{.*}}
// OGCG:   ret void

// sincospi — double

void test_sincospi(double x, double *sinp, double *cosp) {
  __builtin_sincospi(x, sinp, cosp);
}

// CIR-LABEL: @test_sincospi
// CIR:   cir.call @__builtin_sincospi({{.*}}) : (!cir.double, !cir.ptr<!cir.double>, !cir.ptr<!cir.double>) -> ()

// CHECK-LABEL: define {{.*}} @test_sincospi(
// CHECK:   %[[CALL:.*]] = call { double, double } @llvm.sincospi.f64(double %{{.*}})
// CHECK:   %[[SIN:.*]] = extractvalue { double, double } %[[CALL]], 0
// CHECK:   %[[COS:.*]] = extractvalue { double, double } %[[CALL]], 1
// CHECK:   store double %[[SIN]], ptr %{{.*}}
// CHECK:   store double %[[COS]], ptr %{{.*}}
// CHECK:   ret void

// OGCG-LABEL: define {{.*}} @test_sincospi(
// OGCG:   %[[CALL:.*]] = call { double, double } @llvm.sincospi.f64(double %{{.*}})
// OGCG:   %[[SIN:.*]] = extractvalue { double, double } %[[CALL]], 0
// OGCG:   %[[COS:.*]] = extractvalue { double, double } %[[CALL]], 1
// OGCG:   store double %[[SIN]], ptr %{{.*}}
// OGCG:   store double %[[COS]], ptr %{{.*}}
// OGCG:   ret void

// sincospi — long double

void test_sincospil(long double x, long double *sinp, long double *cosp) {
  __builtin_sincospil(x, sinp, cosp);
}

// CIR-LABEL: @test_sincospil
// CIR:   cir.call @__builtin_sincospil({{.*}}) : (!cir.long_double<!cir.f80>, !cir.ptr<!cir.long_double<!cir.f80>>, !cir.ptr<!cir.long_double<!cir.f80>>) -> ()

// CHECK-LABEL: define {{.*}} @test_sincospil(
// CHECK:   %[[CALL:.*]] = call { x86_fp80, x86_fp80 } @llvm.sincospi.f80(x86_fp80 %{{.*}})
// CHECK:   %[[SIN:.*]] = extractvalue { x86_fp80, x86_fp80 } %[[CALL]], 0
// CHECK:   %[[COS:.*]] = extractvalue { x86_fp80, x86_fp80 } %[[CALL]], 1
// CHECK:   store x86_fp80 %[[SIN]], ptr %{{.*}}
// CHECK:   store x86_fp80 %[[COS]], ptr %{{.*}}
// CHECK:   ret void

// OGCG-LABEL: define {{.*}} @test_sincospil(
// OGCG:   %[[CALL:.*]] = call { x86_fp80, x86_fp80 } @llvm.sincospi.f80(x86_fp80 %{{.*}})
// OGCG:   %[[SIN:.*]] = extractvalue { x86_fp80, x86_fp80 } %[[CALL]], 0
// OGCG:   %[[COS:.*]] = extractvalue { x86_fp80, x86_fp80 } %[[CALL]], 1
// OGCG:   store x86_fp80 %[[SIN]], ptr %{{.*}}
// OGCG:   store x86_fp80 %[[COS]], ptr %{{.*}}
// OGCG:   ret void
