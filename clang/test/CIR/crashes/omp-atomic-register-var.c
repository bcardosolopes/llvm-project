// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-version=50 -fclangir -emit-llvm -x c %s -o - | FileCheck %s
// REQUIRES: x86-registered-target
//
// OpenMP atomic with register variable operand.

register int rix __asm__("esp");
int iv;
int ix;
double dv;

void atomic_read_reg(void) {
  // CHECK-LABEL: define{{.*}} @atomic_read_reg(
  // CHECK: call i32 @llvm.read_register.i32(
  // CHECK: call{{.*}} @__kmpc_flush(
  // CHECK: sitofp i32 %{{.*}} to double
  // CHECK: store double %{{.*}}, ptr @dv
  #pragma omp atomic read seq_cst
  dv = rix;
}

void atomic_write_reg(void) {
  // CHECK-LABEL: define{{.*}} @atomic_write_reg(
  // CHECK: load double, ptr @dv
  // CHECK: fptosi double %{{.*}} to i32
  // CHECK: call void @llvm.write_register.i32(
  // CHECK: call{{.*}} @__kmpc_flush(
  #pragma omp atomic write seq_cst
  rix = dv;
}

void atomic_update_reg(void) {
  // CHECK-LABEL: define{{.*}} @atomic_update_reg(
  // CHECK: load double, ptr @dv
  // CHECK: call i32 @llvm.read_register.i32(
  // CHECK: sitofp i32 %{{.*}} to double
  // CHECK: fdiv double %{{.*}}, %{{.*}}
  // CHECK: fptosi double %{{.*}} to i32
  // CHECK: call void @llvm.write_register.i32(
  // CHECK: call{{.*}} @__kmpc_flush(
  #pragma omp atomic seq_cst
  rix = dv / rix;
}

void atomic_capture_reg(void) {
  // CHECK-LABEL: define{{.*}} @atomic_capture_reg(
  // CHECK: load double, ptr @dv
  // CHECK: [[OLD_REG:%.+]] = call i32 @llvm.read_register.i32(
  // CHECK: sitofp i32 [[OLD_REG]] to double
  // CHECK: fdiv double %{{.*}}, %{{.*}}
  // CHECK: [[NEW_REG:%.+]] = fptosi double %{{.*}} to i32
  // CHECK: call void @llvm.write_register.i32({{.*}}, i32 [[NEW_REG]])
  // CHECK: store i32 [[OLD_REG]], ptr @iv
  // CHECK: call{{.*}} @__kmpc_flush(
  #pragma omp atomic capture seq_cst
  { iv = rix; rix = dv / rix; }
}

void atomic_capture_v_reg(void) {
  // CHECK-LABEL: define{{.*}} @atomic_capture_v_reg(
  // CHECK: atomicrmw xchg ptr @ix, i32 5 seq_cst, align 4
  // CHECK: call void @llvm.write_register.i32(
  #pragma omp atomic capture seq_cst
  { rix = ix; ix = 5; }
}
