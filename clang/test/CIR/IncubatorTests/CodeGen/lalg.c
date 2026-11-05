// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -Wno-unused-value -fclangir -emit-cir %s -o - | FileCheck %s

double dot() {
  double x = 0.0;
  double y = 0.0f;
  double result = x * y;
  return result;
}

//      CHECK: %[[X:.*]] = cir.alloca !cir.double, !cir.ptr<!cir.double>, ["x", init]
// CHECK-NEXT: %[[Y:.*]] = cir.alloca !cir.double, !cir.ptr<!cir.double>, ["y", init]
// CHECK-NEXT: %[[RESULT:.*]] = cir.alloca !cir.double, !cir.ptr<!cir.double>, ["result", init]
// CHECK-NEXT: %[[ZERO_D:.*]] = cir.const #cir.fp<0.000000e+00> : !cir.double
// CHECK-NEXT: cir.store{{.*}} %[[ZERO_D]], %[[X]] : !cir.double, !cir.ptr<!cir.double>
// CHECK-NEXT: %[[ZERO_F:.*]] = cir.const #cir.fp<0.000000e+00> : !cir.float
// CHECK-NEXT: %[[CAST:.*]] = cir.cast floating %[[ZERO_F]] : !cir.float -> !cir.double
// CHECK-NEXT: cir.store{{.*}} %[[CAST]], %[[Y]] : !cir.double, !cir.ptr<!cir.double>
// CHECK-NEXT: %[[XVAL:.*]] = cir.load{{.*}} %[[X]] : !cir.ptr<!cir.double>, !cir.double
// CHECK-NEXT: %[[YVAL:.*]] = cir.load{{.*}} %[[Y]] : !cir.ptr<!cir.double>, !cir.double
// CHECK-NEXT: %[[MUL:.*]] = cir.binop(mul, %[[XVAL]], %[[YVAL]]) : !cir.double
