// RUN: %clang_cc1 -triple x86_64-linux-gnu -std=c++17 \
// RUN:   -gkey-instructions -gno-column-info -debug-info-kind=line-tables-only \
// RUN:   -emit-llvm -fclangir -fexceptions -fcxx-exceptions %s -o - \
// RUN:   | FileCheck %s --implicit-check-not=atomGroup --implicit-check-not=atomRank

struct Range {
  int *begin();
  int *end();
} r;

void range_for_empty() {
  for (int i : r)
    ;
}

void range_for_if() {
  for (int i : r) {
    if (i)
      ;
  }
}

// CHECK-LABEL: define dso_local void @_Z15range_for_emptyv(
// CHECK: store ptr @r, ptr %{{.*}}, align 8, !dbg [[A_G1R1:!.*]]
// CHECK: %[[A_RANGE_BEGIN:[0-9]+]] = call noundef ptr @_ZN5Range5beginEv(ptr noundef nonnull align 1 dereferenceable(1) @r), !dbg [[A_G2R2:!.*]]
// CHECK: store ptr %[[A_RANGE_BEGIN]], ptr %{{.*}}, align 8, !dbg [[A_G2R1:!.*]]
// CHECK: %[[A_RANGE_END:[0-9]+]] = call noundef ptr @_ZN5Range3endEv(ptr noundef nonnull align 1 dereferenceable(1) @r), !dbg [[A_G3R2:!.*]]
// CHECK: store ptr %[[A_RANGE_END]], ptr %{{.*}}, align 8, !dbg [[A_G3R1:!.*]]
// CHECK: %[[A_CMP:[0-9]+]] = icmp ne ptr %{{.*}}, %{{.*}}, !dbg [[A_G4R1:!.*]]
// CHECK: br i1 %[[A_CMP]], label %{{.*}}, label %{{.*}}, !dbg [[A_G5R1:!.*]]
// CHECK: %[[A_ELEM:[0-9]+]] = load i32, ptr %{{.*}}, align 4, !dbg [[A_G6R2:!.*]]
// CHECK: store i32 %[[A_ELEM]], ptr %{{.*}}, align 4, !dbg [[A_G6R1:!.*]]
// CHECK: br label %{{.*}}, !dbg [[A_G8R1:!.*]]
// CHECK: %[[A_STEP_BASE:[0-9]+]] = load ptr, ptr %{{.*}}, align 8, !dbg [[A_RANGEDBG:!.*]]
// CHECK: %[[A_STEP_PTR:[0-9]+]] = getelementptr inbounds nuw i32, ptr %[[A_STEP_BASE]], i64 1, !dbg [[A_G7R2:!.*]]
// CHECK: store ptr %[[A_STEP_PTR]], ptr %{{.*}}, align 8, !dbg [[A_G7R1:!.*]]
// CHECK: br label %{{.*}}, !dbg ![[#]], !llvm.loop [[A_LOOPMD:![0-9]+]]
// CHECK: ret void, !dbg [[A_RET:!.*]]

// CHECK-LABEL: define dso_local void @_Z12range_for_ifv(
// CHECK: store ptr @r, ptr %{{.*}}, align 8, !dbg [[B_G1R1:!.*]]
// CHECK: %[[B_CMP:[0-9]+]] = icmp ne ptr %{{.*}}, %{{.*}}, !dbg [[B_G4R1:!.*]]
// CHECK: br i1 %[[B_CMP]], label %{{.*}}, label %[[B_FOREND:[0-9]+]], !dbg [[B_G5R1:!.*]]
// CHECK: %[[B_ELEM:[0-9]+]] = load i32, ptr %{{.*}}, align 4, !dbg [[B_G6R2:!.*]]
// CHECK: store i32 %[[B_ELEM]], ptr %{{.*}}, align 4, !dbg [[B_G6R1:!.*]]
// CHECK: %[[B_IFLOAD:[0-9]+]] = load i32, ptr %{{.*}}, align 4, !dbg [[B_IFLOADDBG:!.*]]
// CHECK: %[[B_IFCMP:[0-9]+]] = icmp ne i32 %[[B_IFLOAD]], 0, !dbg [[B_G7R2:!.*]]
// CHECK: br i1 %[[B_IFCMP]], label %[[B_THEN:[0-9]+]], label %[[B_IFEND:[0-9]+]], !dbg [[B_G7R1:!.*]]
// CHECK: [[B_THEN]]:
// CHECK-NEXT: br label %[[B_IFEND]], !dbg [[B_IFLOADDBG]]
// CHECK: [[B_IFEND]]:
// CHECK-NEXT: br label %[[B_STEP:[0-9]+]], !dbg [[B_G9R1:!.*]]
// CHECK: [[B_STEP]]:
// CHECK-NEXT: %[[B_STEP_BASE:[0-9]+]] = load ptr, ptr %{{.*}}, align 8, !dbg [[B_RANGEDBG:!.*]]
// CHECK: %[[B_STEP_PTR:[0-9]+]] = getelementptr inbounds nuw i32, ptr %[[B_STEP_BASE]], i64 1, !dbg [[B_G8R2:!.*]]
// CHECK: store ptr %[[B_STEP_PTR]], ptr %{{.*}}, align 8, !dbg [[B_G8R1:!.*]]
// CHECK: br label %{{.*}}, !dbg ![[#]], !llvm.loop [[B_LOOPMD:![0-9]+]]
// CHECK: [[B_FOREND]]:
// CHECK-NEXT: ret void, !dbg [[B_RET:!.*]]

// CHECK-DAG: [[A_RANGEDBG]] = !DILocation(line: 12, scope: ![[#]])
// CHECK-DAG: [[A_G1R1]] = !DILocation(line: 12, scope: ![[#]], atomGroup: 1, atomRank: 1)
// CHECK-DAG: [[A_G2R2]] = !DILocation(line: 12, scope: ![[#]], atomGroup: 2, atomRank: 2)
// CHECK-DAG: [[A_G2R1]] = !DILocation(line: 12, scope: ![[#]], atomGroup: 2, atomRank: 1)
// CHECK-DAG: [[A_G3R2]] = !DILocation(line: 12, scope: ![[#]], atomGroup: 3, atomRank: 2)
// CHECK-DAG: [[A_G3R1]] = !DILocation(line: 12, scope: ![[#]], atomGroup: 3, atomRank: 1)
// CHECK-DAG: [[A_G4R1]] = !DILocation(line: 12, scope: ![[#]], atomGroup: 4, atomRank: 1)
// CHECK-DAG: [[A_G5R1]] = !DILocation(line: 12, scope: ![[#]], atomGroup: 5, atomRank: 1)
// CHECK-DAG: [[A_G6R2]] = !DILocation(line: 12, scope: ![[#]], atomGroup: 6, atomRank: 2)
// CHECK-DAG: [[A_G6R1]] = !DILocation(line: 12, scope: ![[#]], atomGroup: 6, atomRank: 1)
// CHECK-DAG: [[A_G7R2]] = !DILocation(line: 12, scope: ![[#]], atomGroup: 7, atomRank: 2)
// CHECK-DAG: [[A_G7R1]] = !DILocation(line: 12, scope: ![[#]], atomGroup: 7, atomRank: 1)
// CHECK-DAG: [[A_G8R1]] = !DILocation(line: 12, scope: ![[#]], atomGroup: 8, atomRank: 1)
// CHECK-DAG: [[A_RET]] = !DILocation(line: 14, scope: ![[#]], atomGroup: 9, atomRank: 1)
// CHECK-DAG: [[A_LOOPMD]] = distinct !{[[A_LOOPMD]], [[A_RANGEDBG]], [[A_LOOP_END:![0-9]+]]}
// CHECK-DAG: [[A_LOOP_END]] = !DILocation(line: 13, scope: ![[#]])

// CHECK-DAG: [[B_RANGEDBG]] = !DILocation(line: 17, scope: ![[#]])
// CHECK-DAG: [[B_G1R1]] = !DILocation(line: 17, scope: ![[#]], atomGroup: 1, atomRank: 1)
// CHECK-DAG: [[B_G4R1]] = !DILocation(line: 17, scope: ![[#]], atomGroup: 4, atomRank: 1)
// CHECK-DAG: [[B_G5R1]] = !DILocation(line: 17, scope: ![[#]], atomGroup: 5, atomRank: 1)
// CHECK-DAG: [[B_G6R2]] = !DILocation(line: 17, scope: ![[#]], atomGroup: 6, atomRank: 2)
// CHECK-DAG: [[B_G6R1]] = !DILocation(line: 17, scope: ![[#]], atomGroup: 6, atomRank: 1)
// CHECK-DAG: [[B_IFLOADDBG]] = !DILocation(line: 18, scope: ![[#]])
// CHECK-DAG: [[B_G7R2]] = !DILocation(line: 18, scope: ![[#]], atomGroup: 7, atomRank: 2)
// CHECK-DAG: [[B_G7R1]] = !DILocation(line: 18, scope: ![[#]], atomGroup: 7, atomRank: 1)
// CHECK-DAG: [[B_G8R2]] = !DILocation(line: 17, scope: ![[#]], atomGroup: 8, atomRank: 2)
// CHECK-DAG: [[B_G8R1]] = !DILocation(line: 17, scope: ![[#]], atomGroup: 8, atomRank: 1)
// CHECK-DAG: [[B_G9R1]] = !DILocation(line: 17, scope: ![[#]], atomGroup: 9, atomRank: 1)
// CHECK-DAG: [[B_RET]] = !DILocation(line: 21, scope: ![[#]], atomGroup: 10, atomRank: 1)
// CHECK-DAG: [[B_LOOPMD]] = distinct !{[[B_LOOPMD]], [[B_RANGEDBG]], [[B_LOOP_END:![0-9]+]]}
// CHECK-DAG: [[B_LOOP_END]] = !DILocation(line: 20, scope: ![[#]])
