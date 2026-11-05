// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --check-prefix=CHECK --input-file=%t.cir.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=CHECK --input-file=%t.ogcg.ll %s

typedef struct {
  float x, y, z;
} vec3f;

typedef struct {
  double x, y;
} vec2d;

double vec3f_test(__builtin_va_list ap) {
  vec3f vec = __builtin_va_arg(ap, vec3f);
  return vec.x + vec.y + vec.z;
}

// CHECK-LABEL: define {{.*}} double @vec3f_test
// CHECK: load <2 x float>, ptr {{.*}}, align 16
// CHECK: store <2 x float> {{.*}}, ptr {{.*}}, align 4
// CHECK: load float, ptr {{.*}}, align 16
// CHECK: store float {{.*}}, ptr {{.*}}, align 4

double vec2d_test(__builtin_va_list ap) {
  vec2d vec = __builtin_va_arg(ap, vec2d);
  return vec.x + vec.y;
}

// CHECK-LABEL: define {{.*}} double @vec2d_test
// CHECK: load double, ptr {{.*}}, align 16
// CHECK: store double {{.*}}, ptr {{.*}}, align 8
// CHECK: load double, ptr {{.*}}, align 16
// CHECK: store double {{.*}}, ptr {{.*}}, align 8
