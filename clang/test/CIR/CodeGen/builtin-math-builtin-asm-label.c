// RUN: %clang_cc1 -std=gnu17 -triple x86_64-unknown-linux-gnu -fclangir \
// RUN:   -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -std=gnu17 -triple x86_64-unknown-linux-gnu -fclangir \
// RUN:   -emit-llvm -disable-llvm-passes %s -o %t.cir.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.cir.ll %s
// RUN: %clang_cc1 -std=gnu17 -triple x86_64-unknown-linux-gnu \
// RUN:   -emit-llvm -disable-llvm-passes %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

double __builtin_frexp(double, int *) asm("__builtin_frexp");
double __builtin_modf(double, double *) asm("__builtin_modf");
void __builtin_sincos(double, double *, double *) asm("__builtin_sincos");
void __builtin_sincospi(double, double *, double *) asm("__builtin_sincospi");

double test_builtin_frexp_same_asm_label(double x, int *exp) {
  return __builtin_frexp(x, exp);
}

double test_builtin_modf_same_asm_label(double x, double *iptr) {
  return __builtin_modf(x, iptr);
}

void test_builtin_sincos_same_asm_label(double x, double *sinp,
                                        double *cosp) {
  __builtin_sincos(x, sinp, cosp);
}

void test_builtin_sincospi_same_asm_label(double x, double *sinp,
                                          double *cosp) {
  __builtin_sincospi(x, sinp, cosp);
}

// CIR-LABEL: cir.func {{.*}} @test_builtin_frexp_same_asm_label
// CIR: cir.call @__builtin_frexp(%{{.*}}, %{{.*}})

// CIR-LABEL: cir.func {{.*}} @test_builtin_modf_same_asm_label
// CIR: cir.call @__builtin_modf(%{{.*}}, %{{.*}})

// CIR-LABEL: cir.func {{.*}} @test_builtin_sincos_same_asm_label
// CIR: cir.call @__builtin_sincos(%{{.*}}, %{{.*}}, %{{.*}})

// CIR-LABEL: cir.func {{.*}} @test_builtin_sincospi_same_asm_label
// CIR: cir.call @__builtin_sincospi(%{{.*}}, %{{.*}}, %{{.*}})

// LLVM-NOT: @llvm.frexp.f64.i32
// LLVM-NOT: @llvm.modf.f64
// LLVM-NOT: @llvm.sincos.f64
// LLVM-NOT: @llvm.sincospi.f64

// LLVM-LABEL: define {{.*}} @test_builtin_frexp_same_asm_label(
// LLVM: call double @__builtin_frexp(double noundef %{{.*}}, ptr noundef %{{.*}})

// LLVM-LABEL: define {{.*}} @test_builtin_modf_same_asm_label(
// LLVM: call double @__builtin_modf(double noundef %{{.*}}, ptr noundef %{{.*}})

// LLVM-LABEL: define {{.*}} @test_builtin_sincos_same_asm_label(
// LLVM: call void @__builtin_sincos(double noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}})

// LLVM-LABEL: define {{.*}} @test_builtin_sincospi_same_asm_label(
// LLVM: call void @__builtin_sincospi(double noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}})

// OGCG-NOT: @llvm.frexp.f64.i32
// OGCG-NOT: @llvm.modf.f64
// OGCG-NOT: @llvm.sincos.f64
// OGCG-NOT: @llvm.sincospi.f64

// OGCG-LABEL: define {{.*}} @test_builtin_frexp_same_asm_label(
// OGCG: call double @__builtin_frexp(double noundef %{{.*}}, ptr noundef %{{.*}})

// OGCG-LABEL: define {{.*}} @test_builtin_modf_same_asm_label(
// OGCG: call double @__builtin_modf(double noundef %{{.*}}, ptr noundef %{{.*}})

// OGCG-LABEL: define {{.*}} @test_builtin_sincos_same_asm_label(
// OGCG: call void @__builtin_sincos(double noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}})

// OGCG-LABEL: define {{.*}} @test_builtin_sincospi_same_asm_label(
// OGCG: call void @__builtin_sincospi(double noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}})
