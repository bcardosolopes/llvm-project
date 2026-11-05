// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=CIR-DECLARE
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -fclangir-mem2reg \
// RUN:   -emit-llvm -debug-info-kind=limited %s -o %t.mem2reg.ll
// RUN: FileCheck --input-file=%t.mem2reg.ll %s --check-prefix=PROMOTED
// RUN: %clang -target x86_64-unknown-linux-gnu -g -S -emit-llvm -fclangir %s \
// RUN:   -o %t.driver.ll
// RUN: FileCheck --input-file=%t.driver.ll %s --check-prefix=DRIVER
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Test that direct-CIR preserves local variable debug info before and after mem2reg.

int compute(int x, int y) {
  int sum = x + y;
  double ratio = 3.14;
  return sum;
}

int nested_scope(int x) {
  int outer = x;
  {
    int inner = outer + 1;
    outer += inner;
  }
  return outer;
}

// Non-promoted CIR and OGCG should emit dbg_declare for parameters and locals.

// CIR-DECLARE-DAG: #dbg_declare(ptr %{{[0-9]+}}, ![[X:[0-9]+]], !DIExpression()
// CIR-DECLARE-DAG: #dbg_declare(ptr %{{[0-9]+}}, ![[Y:[0-9]+]], !DIExpression()
// CIR-DECLARE-DAG: #dbg_declare(ptr %{{[0-9]+}}, ![[SUM:[0-9]+]], !DIExpression()
// CIR-DECLARE-DAG: #dbg_declare(ptr %{{[0-9]+}}, ![[RATIO:[0-9]+]], !DIExpression()
// CIR-DECLARE-DAG: ![[X]] = !DILocalVariable(name: "x",{{.*}}arg: 1,{{.*}}scope: !{{[0-9]+}},{{.*}}type: ![[INT:[0-9]+]])
// CIR-DECLARE-DAG: ![[Y]] = !DILocalVariable(name: "y",{{.*}}arg: 2,{{.*}}scope: !{{[0-9]+}},{{.*}}type: ![[INT]])
// CIR-DECLARE-DAG: ![[SUM]] = !DILocalVariable(name: "sum", scope: !{{[0-9]+}},{{.*}}type: ![[INT]])
// CIR-DECLARE-DAG: ![[RATIO]] = !DILocalVariable(name: "ratio", scope: !{{[0-9]+}},{{.*}}type: ![[DOUBLE:[0-9]+]])
// CIR-DECLARE-DAG: ![[INT]] = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
// CIR-DECLARE-DAG: ![[DOUBLE]] = !DIBasicType(name: "double", size: 64, encoding: DW_ATE_float)
// CIR-DECLARE-DAG: ![[NESTED_FN:[0-9]+]] = distinct !DISubprogram(name: "nested_scope"
// CIR-DECLARE-DAG: ![[INNER_SCOPE:[0-9]+]] = distinct !DILexicalBlock(scope: ![[NESTED_FN]], {{.*}}line: 24, column: 3)
// CIR-DECLARE-DAG: ![[INNER:[0-9]+]] = !DILocalVariable(name: "inner", scope: ![[INNER_SCOPE]]
// CIR-DECLARE-DAG: !DILocation(line: 25, column: 17, scope: ![[INNER_SCOPE]])
// CIR-DECLARE-DAG: !DILocation(line: 26, column: 11, scope: ![[INNER_SCOPE]])

// Promoted CIR should rebuild variable locations with dbg_value and no allocas.
// PROMOTED-LABEL: define dso_local i32 @compute(
// PROMOTED-NOT: alloca
// PROMOTED: #dbg_value(i32 %0, ![[PX:[0-9]+]], !DIExpression(), ![[PCOMPUTE_LOC:[0-9]+]])
// PROMOTED: #dbg_value(i32 %1, ![[PY:[0-9]+]], !DIExpression(), ![[PCOMPUTE_LOC]])
// PROMOTED: %[[PSUM_VAL:[0-9]+]] = add nsw i32 %0, %1, !dbg ![[PSUM_INST_LOC:[0-9]+]]
// PROMOTED: #dbg_value(i32 %[[PSUM_VAL]], ![[PSUM:[0-9]+]], !DIExpression(), ![[PSUM_DECL_LOC:[0-9]+]])
// PROMOTED: #dbg_value(double 3.140000e+00, ![[PRATIO:[0-9]+]], !DIExpression(), ![[PRATIO_DECL_LOC:[0-9]+]])
// PROMOTED: ret i32 %[[PSUM_VAL]], !dbg !{{[0-9]+}}
// PROMOTED-LABEL: define dso_local i32 @nested_scope(
// PROMOTED-NOT: alloca
// PROMOTED: #dbg_value(i32 %0, ![[PNESTED_X:[0-9]+]], !DIExpression(), ![[PNESTED_ENTRY_LOC:[0-9]+]])
// PROMOTED: #dbg_value(i32 %0, ![[POUTER:[0-9]+]], !DIExpression(), ![[POUTER_LOC:[0-9]+]])
// PROMOTED: %[[PINNER_VAL:[0-9]+]] = add nsw i32 %0, 1, !dbg ![[PINNER_INST_LOC:[0-9]+]]
// PROMOTED: #dbg_value(i32 %[[PINNER_VAL]], ![[PINNER:[0-9]+]], !DIExpression(), ![[PINNER_DECL_LOC:[0-9]+]])
// PROMOTED: %[[POUTER_UPDATED:[0-9]+]] = add nsw i32 %0, %[[PINNER_VAL]], !dbg ![[POUTER_UPDATE_LOC:[0-9]+]]
// PROMOTED: #dbg_value(i32 %[[POUTER_UPDATED]], ![[POUTER]], !DIExpression(), ![[POUTER_LOC]])
// PROMOTED-DAG: ![[PCOMPUTE_FN:[0-9]+]] = distinct !DISubprogram(name: "compute"
// PROMOTED-DAG: ![[PX]] = !DILocalVariable(name: "x",{{.*}}arg: 1, scope: ![[PCOMPUTE_FN]],{{.*}}type: ![[PINT:[0-9]+]])
// PROMOTED-DAG: ![[PY]] = !DILocalVariable(name: "y",{{.*}}arg: 2, scope: ![[PCOMPUTE_FN]],{{.*}}type: ![[PINT]])
// PROMOTED-DAG: ![[PSUM]] = !DILocalVariable(name: "sum", scope: ![[PCOMPUTE_FN]],{{.*}}type: ![[PINT]])
// PROMOTED-DAG: ![[PRATIO]] = !DILocalVariable(name: "ratio", scope: ![[PCOMPUTE_FN]],{{.*}}type: ![[PDOUBLE:[0-9]+]])
// PROMOTED-DAG: ![[PINT]] = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
// PROMOTED-DAG: ![[PDOUBLE]] = !DIBasicType(name: "double", size: 64, encoding: DW_ATE_float)
// PROMOTED-DAG: ![[PCOMPUTE_LOC]] = !DILocation(line: 16, scope: ![[PCOMPUTE_FN]])
// PROMOTED-DAG: ![[PSUM_INST_LOC]] = !DILocation(line: 17, scope: ![[PCOMPUTE_FN]])
// PROMOTED-DAG: ![[PSUM_DECL_LOC]] = !DILocation(line: 17, column: 7, scope: ![[PCOMPUTE_FN]])
// PROMOTED-DAG: ![[PRATIO_DECL_LOC]] = !DILocation(line: 18, column: 10, scope: ![[PCOMPUTE_FN]])
// PROMOTED-DAG: ![[PNESTED_FN:[0-9]+]] = distinct !DISubprogram(name: "nested_scope"
// PROMOTED-DAG: ![[PNESTED_X]] = !DILocalVariable(name: "x",{{.*}}arg: 1, scope: ![[PNESTED_FN]],{{.*}}type: ![[PINT]])
// PROMOTED-DAG: ![[POUTER]] = !DILocalVariable(name: "outer", scope: ![[PNESTED_FN]],{{.*}}type: ![[PINT]])
// PROMOTED-DAG: ![[POUTER_LOC]] = !DILocation(line: 23, column: 7, scope: ![[PNESTED_FN]])
// PROMOTED-DAG: ![[PNESTED_ENTRY_LOC]] = !DILocation(line: 22, scope: ![[PNESTED_FN]])
// PROMOTED-DAG: ![[PINNER_SCOPE:[0-9]+]] = distinct !DILexicalBlock(scope: ![[PNESTED_FN]], {{.*}}line: 24, column: 3)
// PROMOTED-DAG: ![[PINNER]] = !DILocalVariable(name: "inner", scope: ![[PINNER_SCOPE]]
// PROMOTED-DAG: ![[PINNER_INST_LOC]] = !DILocation(line: 25, scope: ![[PINNER_SCOPE]])
// PROMOTED-DAG: ![[PINNER_DECL_LOC]] = !DILocation(line: 25, column: 9, scope: ![[PINNER_SCOPE]])
// PROMOTED-DAG: ![[POUTER_UPDATE_LOC]] = !DILocation(line: 26, scope: ![[PINNER_SCOPE]])

// OGCG-DAG: #dbg_declare(ptr %{{.*}}, ![[X:[0-9]+]], !DIExpression()
// OGCG-DAG: #dbg_declare(ptr %{{.*}}, ![[Y:[0-9]+]], !DIExpression()
// OGCG-DAG: #dbg_declare(ptr %{{.*}}, ![[SUM:[0-9]+]], !DIExpression()
// OGCG-DAG: #dbg_declare(ptr %{{.*}}, ![[RATIO:[0-9]+]], !DIExpression()
// OGCG-DAG: ![[X]] = !DILocalVariable(name: "x",{{.*}}arg: 1,{{.*}}type: ![[INT:[0-9]+]])
// OGCG-DAG: ![[Y]] = !DILocalVariable(name: "y",{{.*}}arg: 2,{{.*}}type: ![[INT]])
// OGCG-DAG: ![[SUM]] = !DILocalVariable(name: "sum",{{.*}}type: ![[INT]])
// OGCG-DAG: ![[RATIO]] = !DILocalVariable(name: "ratio",{{.*}}type: ![[DOUBLE:[0-9]+]])
// OGCG-DAG: ![[INT]] = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
// OGCG-DAG: ![[DOUBLE]] = !DIBasicType(name: "double", size: 64, encoding: DW_ATE_float)
// OGCG-DAG: ![[NESTED_FN:[0-9]+]] = distinct !DISubprogram(name: "nested_scope"
// OGCG-DAG: ![[INNER_SCOPE:[0-9]+]] = distinct !DILexicalBlock(scope: ![[NESTED_FN]], {{.*}}line: 24, column: 3)
// OGCG-DAG: ![[INNER:[0-9]+]] = !DILocalVariable(name: "inner", scope: ![[INNER_SCOPE]]
// OGCG-DAG: !DILocation(line: 25, column: 17, scope: ![[INNER_SCOPE]])
// OGCG-DAG: !DILocation(line: 26, column: 11, scope: ![[INNER_SCOPE]])

// At -O0 (the driver's default), the driver must NOT enable mem2reg when debug
// info is requested: it keeps the source allocas and emits dbg_declare for every
// local/parameter (matching classic -O0), so each variable retains a memory
// location. This preserves locations even for types the -O0 backend cannot
// express as dbg_value (x86 long double, __int128, _Complex).
// DRIVER: #dbg_declare(
// DRIVER-NOT: #dbg_value(
