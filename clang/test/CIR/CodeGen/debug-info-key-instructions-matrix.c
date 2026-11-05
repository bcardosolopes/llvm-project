// RUN: %clang_cc1 -triple x86_64-linux-gnu -gkey-instructions -debug-info-kind=line-tables-only -fenable-matrix -fclangir %s -emit-llvm -o - | FileCheck %s

typedef float m5x5 __attribute__((matrix_type(5, 5)));
m5x5 m;

void fun(void) {
// CHECK-LABEL: define{{.*}} @fun(
// CHECK: [[LOAD:%.*]] = load <25 x float>, ptr @m, align 4
// CHECK-NEXT: [[INS:%.*]] = insertelement <25 x float> [[LOAD]], float 0.000000e+00, i64 0, !dbg [[G1R2:!.*]]
// CHECK-NEXT: store <25 x float> [[INS]], ptr @m, align 4, !dbg [[G1R1:!.*]]
// CHECK: ret void, !dbg [[RET:!.*]]
  m[0][0] = 0;
}

// CHECK: [[G1R2]] = !DILocation({{.*}}, atomGroup: 1, atomRank: 2)
// CHECK: [[G1R1]] = !DILocation({{.*}}, atomGroup: 1, atomRank: 1)
// CHECK: [[RET]] = !DILocation({{.*}}, atomGroup: 2, atomRank: 1)
