// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t-cir.cir
// RUN: FileCheck --input-file=%t-cir.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -disable-llvm-passes %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -disable-llvm-passes %s -o %t-ogcg.ll
// RUN: FileCheck --input-file=%t-ogcg.ll %s --check-prefix=OGCG

double modf(double, double *) asm("modf");
void sincos(double, double *, double *) asm("sincos");

double test_modf_asm_label_alias(double x, double *iptr) {
  return modf(x, iptr);
}

void test_sincos_asm_label_alias(double x, double *sinp, double *cosp) {
  sincos(x, sinp, cosp);
}

// CIR-LABEL: cir.func {{.*}} @test_modf_asm_label_alias
// CIR: cir.call @modf(%{{.*}}, %{{.*}})
// CIR-NOT: @llvm.modf

// CIR-LABEL: cir.func {{.*}} @test_sincos_asm_label_alias
// CIR: cir.call @sincos(%{{.*}}, %{{.*}}, %{{.*}})
// CIR-NOT: @llvm.sincos

// LLVM-LABEL: define {{.*}} @test_modf_asm_label_alias(
// LLVM: call double @modf(double noundef %{{.*}}, ptr noundef %{{.*}})
// LLVM-LABEL: define {{.*}} @test_sincos_asm_label_alias(
// LLVM: call void @sincos(double noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}})
// LLVM-NOT: @llvm.modf.f64
// LLVM-NOT: @llvm.sincos.f64

// OGCG-LABEL: define {{.*}} @test_modf_asm_label_alias(
// OGCG: call double @modf(double noundef %{{.*}}, ptr noundef %{{.*}})
// OGCG-LABEL: define {{.*}} @test_sincos_asm_label_alias(
// OGCG: call void @sincos(double noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}})
// OGCG-NOT: @llvm.modf.f64
// OGCG-NOT: @llvm.sincos.f64
