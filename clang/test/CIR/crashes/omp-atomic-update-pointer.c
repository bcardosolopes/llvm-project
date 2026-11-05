// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -target-cpu core2 -fopenmp -fclangir -emit-llvm -x c %s -o - | FileCheck %s
//
// OpenMP atomic pointer update.

char *cpx;

int atomic_update_ptr(void) {
  // CHECK-LABEL: define{{.*}} @atomic_update_ptr(
  // CHECK: [[LD_CPX:%.+]] = load atomic ptr, ptr @cpx monotonic, align 8
  // CHECK: br label %[[CONT:.+]]
  // CHECK: [[CONT]]
  // CHECK: [[PHI:%.+]] = phi ptr [ [[LD_CPX]], %{{.+}} ], [ [[OLD:%.+]], %[[CONT]] ]
  // CHECK: [[NEW:%.+]] = getelementptr inbounds i8, ptr [[PHI]], i64 1
  // CHECK: [[RES:%.+]] = cmpxchg ptr @cpx, ptr [[PHI]], ptr [[NEW]] monotonic monotonic, align 8
  // CHECK: [[OLD]] = extractvalue { ptr, i1 } [[RES]], 0
  // CHECK: %{{.+}} = extractvalue { ptr, i1 } [[RES]], 1
  // CHECK: br i1 %{{.+}}, label %[[EXIT:.+]], label %[[CONT]]
  // CHECK: [[EXIT]]
  #pragma omp atomic update
  cpx += 1;
  return 0;
}
