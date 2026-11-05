// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fopenmp-enable-irbuilder -fopenmp -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

void work();

// CHECK-LABEL: cir.func private @__omp_copyprivate_x
// CHECK: %[[SRC:.*]] = cir.load {{.*}} %arg1
// CHECK: cir.store {{.*}} %[[SRC]], %arg0

// CHECK-LABEL: cir.func no_inline optnone dso_local @{{.*}}f
void f() {
  int x = 0;
#pragma omp parallel private(x)
  {
// CHECK: omp.single copyprivate(%arg0 -> @__omp_copyprivate_x
// CHECK-NOT: omp.single nowait
    #pragma omp single copyprivate(x)
    {
      x = 7;
      work();
    }
  }
}
