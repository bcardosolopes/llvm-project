// RUN: %clang_cc1 -triple x86_64-linux-gnu -std=c++17 \
// RUN:   -gkey-instructions -debug-info-kind=line-tables-only \
// RUN:   -emit-llvm -fclangir -fexceptions -fcxx-exceptions %s -o - \
// RUN:   | FileCheck %s --implicit-check-not=atomGroup --implicit-check-not=atomRank

// CHECK: [[B_ADDR:@.*]] = {{.*}}global ptr

void g(int *a) {
  static int &b = *a;
}

// CHECK-LABEL: define dso_local void @_Z1gPi(
// CHECK: %[[V:[0-9]+]] = load ptr, ptr %{{.*}}, align 8, !dbg [[G1R2:!.*]]
// CHECK: store ptr %[[V]], ptr [[B_ADDR]], align 8, !dbg [[G1R1:!.*]]
// CHECK: ret void, !dbg [[RET:!.*]]

// CHECK-DAG: [[G1R2]] = !DILocation(line: 9, column: 20, scope: ![[#]], atomGroup: 1, atomRank: 2)
// CHECK-DAG: [[G1R1]] = !DILocation(line: 9{{.*}}, scope: ![[#]], atomGroup: 1, atomRank: 1)
// CHECK-DAG: [[RET]] = !DILocation(line: 10, column: 1, scope: ![[#]], atomGroup: 2, atomRank: 1)
