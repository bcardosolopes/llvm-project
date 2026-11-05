// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -fclangir \
// RUN:   -emit-llvm -debug-info-kind=limited %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CHECK
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=CHECK

void outer() {
  struct Local {
    int m(int x) { return x; }
  };
  Local l;
  (void)l.m(1);
}

// CHECK-DAG: ![[OUTER:[0-9]+]] = distinct !DISubprogram(name: "outer", linkageName: "_Z5outerv", scope: ![[OUTER_FILE:[0-9]+]], file: ![[OUTER_FILE]], line: {{[0-9]+}}, type: ![[OUTER_TY:[0-9]+]], scopeLine: {{[0-9]+}}, flags: DIFlagPrototyped, spFlags: DISPFlagDefinition
// CHECK-DAG: ![[LOCAL:[0-9]+]] = distinct !DICompositeType(tag: DW_TAG_structure_type, name: "Local", scope: ![[OUTER]], file: ![[LOCAL_FILE:[0-9]+]], line: {{[0-9]+}}, size: 8{{.*}}elements: ![[ELEMS:[0-9]+]]
// CHECK-DAG: ![[ELEMS]] = !{![[METHOD_DECL:[0-9]+]]}
// CHECK-DAG: ![[METHOD_DECL]] = !DISubprogram(name: "m", scope: ![[LOCAL]], file: ![[LOCAL_FILE]], line: {{[0-9]+}}, type: ![[METHOD_TY:[0-9]+]], scopeLine: {{[0-9]+}}, flags: DIFlagPrototyped, spFlags: DISPFlagLocalToUnit)
// CHECK-DAG: ![[METHOD_DEF:[0-9]+]] = distinct !DISubprogram(name: "m", linkageName: "_ZZ5outervEN5Local1mEi", scope: ![[LOCAL]], file: ![[LOCAL_FILE]], line: {{[0-9]+}}, type: ![[METHOD_TY]], scopeLine: {{[0-9]+}}, flags: DIFlagPrototyped, spFlags: DISPFlagLocalToUnit | DISPFlagDefinition{{.*}}declaration: ![[METHOD_DECL]]
// CHECK-DAG: !DILocalVariable(name: "l", scope: ![[OUTER]], file: ![[OUTER_FILE]], line: {{[0-9]+}}, type: ![[LOCAL]])
