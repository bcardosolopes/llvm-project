// RUN: %clang_cc1 -triple x86_64-linux-gnu -gkey-instructions -gno-column-info \
// RUN:   -debug-info-kind=line-tables-only -ftrivial-auto-var-init=pattern \
// RUN:   -emit-llvm -fclangir %s -o - \
// RUN:   | FileCheck %s --implicit-check-not=atomGroup --implicit-check-not=atomRank

int g;

void init_agg_atoms(void) {
  int A[] = {1, 2, 3};
  int B[] = {1, 2, g};
  char big[65536] = {'a', 'b', 'c', 'd'};
  char arr[] = {42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42,
                42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42,
                42, 42, 42, 42, 42};
  char uninit;
}

// CHECK-LABEL: define dso_local void @init_agg_atoms(
// CHECK: %{{.*}} = alloca [33 x i8], i64 1, align 16, !dbg [[ARR_LOC:!.*]]
// CHECK: call void @llvm.memcpy{{.*}}, !dbg [[G1R1:![0-9]+]]
// CHECK: call void @llvm.memcpy{{.*}}, !dbg [[G2R1:![0-9]+]]
// CHECK: store i32 1, ptr %{{.*}}, align 4, !dbg [[G2R1]]
// CHECK: store i32 2, ptr %{{.*}}, align 4, !dbg [[G2R1]]
// CHECK: %[[B_LOAD:[0-9]+]] = load i32, ptr @g, align 4, !dbg [[G2R2:![0-9]+]]
// CHECK: store i32 %[[B_LOAD]], ptr %{{.*}}, align 4, !dbg [[G2R1]]
// CHECK: call void @llvm.memset{{.*}}, !dbg [[G3R1:![0-9]+]]
// CHECK: store i8 97, ptr %{{.*}}, align 16, !dbg [[G3R1]]
// CHECK: store i8 98, ptr %{{.*}}, align 1, !dbg [[G3R1]]
// CHECK: store i8 99, ptr %{{.*}}, align 2, !dbg [[G3R1]]
// CHECK: store i8 100, ptr %{{.*}}, align 1, !dbg [[G3R1]]
// CHECK: call void @llvm.memset{{.*}}, !dbg [[G4R1:![0-9]+]]
// CHECK: store i8 -86, ptr %{{.*}}, align 1, !dbg [[G5R1:![0-9]+]]
// CHECK: ret void, !dbg [[RET:![0-9]+]]

// CHECK: [[ARR_LOC]] = !DILocation(line: 12, scope: ![[#]])
// CHECK: [[G1R1]] = !DILocation(line: 9, scope: ![[#]], atomGroup: 1, atomRank: 1)
// CHECK: [[G2R1]] = !DILocation(line: 10, scope: ![[#]], atomGroup: 2, atomRank: 1)
// CHECK: [[G2R2]] = !DILocation(line: 10, scope: ![[#]], atomGroup: 2, atomRank: 2)
// CHECK: [[G3R1]] = !DILocation(line: 11, scope: ![[#]], atomGroup: 3, atomRank: 1)
// CHECK: [[G4R1]] = !DILocation(line: 12, scope: ![[#]], atomGroup: 4, atomRank: 1)
// CHECK: [[G5R1]] = !DILocation(line: 15, scope: ![[#]], atomGroup: 5, atomRank: 1)
// CHECK: [[RET]] = !DILocation(line: 16, scope: ![[#]], atomGroup: 6, atomRank: 1)
