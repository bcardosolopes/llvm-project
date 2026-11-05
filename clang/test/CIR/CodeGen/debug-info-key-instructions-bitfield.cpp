// RUN: %clang_cc1 -triple x86_64-linux-gnu -std=c++17 \
// RUN:   -gkey-instructions -gno-column-info -debug-info-kind=line-tables-only \
// RUN:   -emit-llvm -fclangir -fexceptions -fcxx-exceptions %s -o - \
// RUN:   | FileCheck %s --implicit-check-not=atomGroup --implicit-check-not=atomRank

struct S { int a : 3; };

void bitfield_assign(int x, S s) {
  // Check bitfield assignments preserve OGCG's key-instruction ranking:
  // the synthesized `or` is rank 2, the final store is rank 1, and the
  // surrounding helper ops remain plain source locations.
  s.a = x;
}

// CHECK-LABEL: define dso_local void @_Z15bitfield_assigni1S(
// CHECK: %[[TMP:[0-9]+]] = load i32, ptr %{{.*}}, align 4, !dbg [[LOADDBG:!.*]]
// CHECK: %[[FIELDPTR:[0-9]+]] = getelementptr inbounds nuw %struct.S, ptr %{{.*}}, i32 0, i32 0, !dbg [[FIELDDbg:!.*]]
// CHECK: %[[TRUNC:[0-9]+]] = trunc i32 %[[TMP]] to i8, !dbg [[LOADDBG]]
// CHECK: %[[OLD:[0-9]+]] = load i8, ptr %[[FIELDPTR]], align 4, !dbg [[LOADDBG]]
// CHECK: %[[MASKED_NEW:[0-9]+]] = and i8 %[[TRUNC]], 7, !dbg [[LOADDBG]]
// CHECK: %[[MASKED_OLD:[0-9]+]] = and i8 %[[OLD]], -8, !dbg [[LOADDBG]]
// CHECK: %[[SET:[0-9]+]] = or i8 %[[MASKED_OLD]], %[[MASKED_NEW]], !dbg [[G1R2:!.*]]
// CHECK: store i8 %[[SET]], ptr %[[FIELDPTR]], align 4, !dbg [[G1R1:!.*]]
// CHECK: ret void, !dbg [[RET:!.*]]

// CHECK-DAG: [[LOADDBG]] = !DILocation(line: 12{{.*}}, scope: ![[#]])
// CHECK-DAG: [[FIELDDbg]] = !DILocation(line: 6{{.*}}, scope: ![[#]])
// CHECK-DAG: [[G1R2]] = !DILocation(line: 12{{.*}}, scope: ![[#]], atomGroup: 1, atomRank: 2)
// CHECK-DAG: [[G1R1]] = !DILocation(line: 12{{.*}}, scope: ![[#]], atomGroup: 1, atomRank: 1)
// CHECK-DAG: [[RET]] = !DILocation(line: 13, scope: ![[#]], atomGroup: 2, atomRank: 1)
