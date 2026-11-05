// RUN: %clang_cc1 -triple x86_64-linux-gnu -std=c++17 \
// RUN:   -gkey-instructions -debug-info-kind=line-tables-only -Wno-unused-variable \
// RUN:   -emit-llvm -fclangir -fexceptions -fcxx-exceptions %s -o - \
// RUN:   | FileCheck %s --implicit-check-not=atomGroup --implicit-check-not=atomRank

void f(int x) {
  int *n = new int(x);
  n = new int(x);
  *n = x;
}

// CHECK-LABEL: define dso_local void @_Z1fi(
// CHECK: %[[CALL0:[0-9]+]] = call noalias noundef nonnull ptr @_Znwm(i64 noundef 4){{.*}}, !dbg [[G1R2CALL:!.*]]
// CHECK: %[[LOAD0:[0-9]+]] = load i32, ptr %{{.*}}, align 4, !dbg [[G1R2LOAD:!.*]]
// CHECK: store i32 %[[LOAD0]], ptr %[[CALL0]], align 4, !dbg [[G1R1STORE:!.*]]
// CHECK: store ptr %[[CALL0]], ptr %{{.*}}, align 8, !dbg [[G1R1PTR:!.*]]
// CHECK: %[[CALL1:[0-9]+]] = call noalias noundef nonnull ptr @_Znwm(i64 noundef 4){{.*}}, !dbg [[G2R2CALL:!.*]]
// CHECK: %[[LOAD1:[0-9]+]] = load i32, ptr %{{.*}}, align 4, !dbg [[G2R2LOAD:!.*]]
// CHECK: store i32 %[[LOAD1]], ptr %[[CALL1]], align 4, !dbg [[G2R1STORE:!.*]]
// CHECK: store ptr %[[CALL1]], ptr %{{.*}}, align 8, !dbg [[G2R1PTR:!.*]]
// CHECK: %[[LOAD2:[0-9]+]] = load i32, ptr %{{.*}}, align 4, !dbg [[G3R2:!.*]]
// CHECK: %[[PTR:[0-9]+]] = load ptr, ptr %{{.*}}, align 8, !dbg [[PLAINPTR:!.*]]
// CHECK: store i32 %[[LOAD2]], ptr %[[PTR]], align 4, !dbg [[G3R1:!.*]]
// CHECK: ret void, !dbg [[RET:!.*]]

// CHECK-DAG: [[G1R2CALL]] = !DILocation(line: 7, column: 12, scope: ![[#]], atomGroup: 1, atomRank: 2)
// CHECK-DAG: [[G1R2LOAD]] = !DILocation(line: 7, column: 20, scope: ![[#]], atomGroup: 1, atomRank: 2)
// CHECK-DAG: [[G1R1STORE]] = !DILocation(line: 7, column: 12, scope: ![[#]], atomGroup: 1, atomRank: 1)
// CHECK-DAG: [[G1R1PTR]] = !DILocation(line: 7{{.*}}, scope: ![[#]], atomGroup: 1, atomRank: 1)
// CHECK-DAG: [[G2R2CALL]] = !DILocation(line: 8, column: 7, scope: ![[#]], atomGroup: 2, atomRank: 2)
// CHECK-DAG: [[G2R2LOAD]] = !DILocation(line: 8, column: 15, scope: ![[#]], atomGroup: 2, atomRank: 2)
// CHECK-DAG: [[G2R1STORE]] = !DILocation(line: 8, column: 7, scope: ![[#]], atomGroup: 2, atomRank: 1)
// CHECK-DAG: [[G2R1PTR]] = !DILocation(line: 8{{.*}}, scope: ![[#]], atomGroup: 2, atomRank: 1)
// CHECK-DAG: [[G3R2]] = !DILocation(line: 9, column: 8, scope: ![[#]], atomGroup: 3, atomRank: 2)
// CHECK-DAG: [[PLAINPTR]] = !DILocation(line: 9, column: 4, scope: ![[#]])
// CHECK-DAG: [[G3R1]] = !DILocation(line: 9{{.*}}, scope: ![[#]], atomGroup: 3, atomRank: 1)
// CHECK-DAG: [[RET]] = !DILocation(line: 10, column: 1, scope: ![[#]], atomGroup: 4, atomRank: 1)
