// RUN: %clang_cc1 -triple aarch64-none-linux-android26 -Oz -fclangir \
// RUN:   -fclangir-mem2reg -emit-llvm -disable-llvm-passes \
// RUN:   -debug-info-kind=constructor -dwarf-version=4 -debugger-tuning=lldb \
// RUN:   -std=gnu++20 -fcxx-exceptions -fexceptions -w -x c++ %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -triple aarch64-none-linux-android26 -Oz \
// RUN:   -emit-llvm -disable-llvm-passes \
// RUN:   -debug-info-kind=constructor -dwarf-version=4 -debugger-tuning=lldb \
// RUN:   -std=gnu++20 -fcxx-exceptions -fexceptions -w -x c++ %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Test that lambda operator() declaration DISubprograms do NOT carry
// DIFlagAllCallsDescribed (which is definition-only) and DO carry
// DISPFlagOptimized + DISPFlagLocalToUnit, matching OGCG.
// Also check that instantiated function-template definitions preserve
// LocalToUnit/templateParams and only retain the real parameter local, while
// the enclosing function does not retain synthetic lambda temporaries.

template <class T>
void foreach(T fn) { fn(1); }

void bar() {
  foreach([](int x) { });
  foreach([](int x) { });
}

// CHECK-DAG: ![[BAR:[0-9]+]] = distinct !DISubprogram(name: "bar", linkageName: "_Z3barv", scope: ![[FILE:[0-9]+]], file: ![[FILE]], line: {{[0-9]+}}, type: ![[BARTY:[0-9]+]], scopeLine: {{[0-9]+}}, flags: DIFlagPrototyped | DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: ![[CU:[0-9]+]])
// CHECK-DAG: ![[FOREACH0:[0-9]+]] = distinct !DISubprogram(name: "foreach<(lambda at {{.*}})>", linkageName: "_Z7foreachIZ3barvE3$_0EvT_", scope: ![[FILE]], file: ![[FILE]], line: {{[0-9]+}}, type: ![[FOREACH0TY:[0-9]+]], scopeLine: {{[0-9]+}}, flags: DIFlagPrototyped | DIFlagAllCallsDescribed, spFlags: DISPFlagLocalToUnit | DISPFlagDefinition | DISPFlagOptimized, unit: ![[CU]], templateParams: ![[FOREACH0TPS:[0-9]+]], retainedNodes: ![[FOREACH0RNS:[0-9]+]])
// CHECK-DAG: ![[FOREACH0TY]] = !DISubroutineType(types: ![[FOREACH0TYS:[0-9]+]])
// CHECK-DAG: ![[FOREACH0TYS]] = !{null, ![[FOREACH0LAM:[0-9]+]]}
// CHECK-DAG: ![[FOREACH0RNS]] = !{![[FOREACH0FN:[0-9]+]]}
// CHECK-DAG: ![[FOREACH0FN]] = !DILocalVariable(name: "fn", arg: 1, scope: ![[FOREACH0]], file: ![[FILE]], line: {{[0-9]+}}, type: ![[FOREACH0LAM]])
// CHECK-DAG: ![[FOREACH0TPS]] = !{![[FOREACH0TP:[0-9]+]]}
// CHECK-DAG: ![[FOREACH0TP]] = !DITemplateTypeParameter(name: "T", type: ![[FOREACH0LAM]])
// CHECK-DAG: ![[FOREACH0LAM]] = distinct !DICompositeType(tag: DW_TAG_class_type{{.*}}scope: ![[BAR]]{{.*}}line: {{[0-9]+}}, size: 8, flags: DIFlagTypePassByValue, elements: ![[EMPTY_ELTS:[0-9]+]])
// CHECK-DAG: ![[FOREACH1:[0-9]+]] = distinct !DISubprogram(name: "foreach<(lambda at {{.*}})>", linkageName: "_Z7foreachIZ3barvE3$_1EvT_", scope: ![[FILE]], file: ![[FILE]], line: {{[0-9]+}}, type: ![[FOREACH1TY:[0-9]+]], scopeLine: {{[0-9]+}}, flags: DIFlagPrototyped | DIFlagAllCallsDescribed, spFlags: DISPFlagLocalToUnit | DISPFlagDefinition | DISPFlagOptimized, unit: ![[CU]], templateParams: ![[FOREACH1TPS:[0-9]+]], retainedNodes: ![[FOREACH1RNS:[0-9]+]])
// CHECK-DAG: ![[FOREACH1TY]] = !DISubroutineType(types: ![[FOREACH1TYS:[0-9]+]])
// CHECK-DAG: ![[FOREACH1TYS]] = !{null, ![[FOREACH1LAM:[0-9]+]]}
// CHECK-DAG: ![[FOREACH1RNS]] = !{![[FOREACH1FN:[0-9]+]]}
// CHECK-DAG: ![[FOREACH1FN]] = !DILocalVariable(name: "fn", arg: 1, scope: ![[FOREACH1]], file: ![[FILE]], line: {{[0-9]+}}, type: ![[FOREACH1LAM]])
// CHECK-DAG: ![[FOREACH1TPS]] = !{![[FOREACH1TP:[0-9]+]]}
// CHECK-DAG: ![[FOREACH1TP]] = !DITemplateTypeParameter(name: "T", type: ![[FOREACH1LAM]])
// CHECK-DAG: ![[FOREACH1LAM]] = distinct !DICompositeType(tag: DW_TAG_class_type{{.*}}scope: ![[BAR]]{{.*}}line: {{[0-9]+}}, size: 8, flags: DIFlagTypePassByValue, elements: ![[EMPTY_ELTS]])
// OGCG-DAG: ![[BAR:[0-9]+]] = distinct !DISubprogram(name: "bar", linkageName: "_Z3barv", scope: ![[FILE:[0-9]+]], file: ![[FILE]], line: {{[0-9]+}}, type: ![[BARTY:[0-9]+]], scopeLine: {{[0-9]+}}, flags: DIFlagPrototyped | DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: ![[CU:[0-9]+]])
// OGCG-DAG: ![[FOREACH0:[0-9]+]] = distinct !DISubprogram(name: "foreach<(lambda at {{.*}})>", linkageName: "_Z7foreachIZ3barvE3$_0EvT_", scope: ![[FILE]], file: ![[FILE]], line: {{[0-9]+}}, type: ![[FOREACH0TY:[0-9]+]], scopeLine: {{[0-9]+}}, flags: DIFlagPrototyped | DIFlagAllCallsDescribed, spFlags: DISPFlagLocalToUnit | DISPFlagDefinition | DISPFlagOptimized, unit: ![[CU]], templateParams: ![[FOREACH0TPS:[0-9]+]], retainedNodes: ![[FOREACH0RNS:[0-9]+]])
// OGCG-DAG: ![[FOREACH0TY]] = !DISubroutineType(types: ![[FOREACH0TYS:[0-9]+]])
// OGCG-DAG: ![[FOREACH0TYS]] = !{null, ![[FOREACH0LAM:[0-9]+]]}
// OGCG-DAG: ![[FOREACH0RNS]] = !{![[FOREACH0FN:[0-9]+]]}
// OGCG-DAG: ![[FOREACH0FN]] = !DILocalVariable(name: "fn", arg: 1, scope: ![[FOREACH0]], file: ![[FILE]], line: {{[0-9]+}}, type: ![[FOREACH0LAM]])
// OGCG-DAG: ![[FOREACH0TPS]] = !{![[FOREACH0TP:[0-9]+]]}
// OGCG-DAG: ![[FOREACH0TP]] = !DITemplateTypeParameter(name: "T", type: ![[FOREACH0LAM]])
// OGCG-DAG: ![[FOREACH0LAM]] = distinct !DICompositeType(tag: DW_TAG_class_type{{.*}}scope: ![[BAR]]{{.*}}line: {{[0-9]+}}, size: 8, flags: DIFlagTypePassByValue, elements: ![[EMPTY_ELTS:[0-9]+]])
// OGCG-DAG: ![[FOREACH1:[0-9]+]] = distinct !DISubprogram(name: "foreach<(lambda at {{.*}})>", linkageName: "_Z7foreachIZ3barvE3$_1EvT_", scope: ![[FILE]], file: ![[FILE]], line: {{[0-9]+}}, type: ![[FOREACH1TY:[0-9]+]], scopeLine: {{[0-9]+}}, flags: DIFlagPrototyped | DIFlagAllCallsDescribed, spFlags: DISPFlagLocalToUnit | DISPFlagDefinition | DISPFlagOptimized, unit: ![[CU]], templateParams: ![[FOREACH1TPS:[0-9]+]], retainedNodes: ![[FOREACH1RNS:[0-9]+]])
// OGCG-DAG: ![[FOREACH1TY]] = !DISubroutineType(types: ![[FOREACH1TYS:[0-9]+]])
// OGCG-DAG: ![[FOREACH1TYS]] = !{null, ![[FOREACH1LAM:[0-9]+]]}
// OGCG-DAG: ![[FOREACH1RNS]] = !{![[FOREACH1FN:[0-9]+]]}
// OGCG-DAG: ![[FOREACH1FN]] = !DILocalVariable(name: "fn", arg: 1, scope: ![[FOREACH1]], file: ![[FILE]], line: {{[0-9]+}}, type: ![[FOREACH1LAM]])
// OGCG-DAG: ![[FOREACH1TPS]] = !{![[FOREACH1TP:[0-9]+]]}
// OGCG-DAG: ![[FOREACH1TP]] = !DITemplateTypeParameter(name: "T", type: ![[FOREACH1LAM]])
// OGCG-DAG: ![[FOREACH1LAM]] = distinct !DICompositeType(tag: DW_TAG_class_type{{.*}}scope: ![[BAR]]{{.*}}line: {{[0-9]+}}, size: 8, flags: DIFlagTypePassByValue, elements: ![[EMPTY_ELTS]])

// Definition should have AllCallsDescribed.
// CHECK-DAG: distinct !DISubprogram(name: "operator()"{{.*}}DIFlagAllCallsDescribed{{.*}}DISPFlagDefinition

// Declaration must NOT have AllCallsDescribed but must have
// DISPFlagOptimized and DISPFlagLocalToUnit.
// CHECK-DAG: !DISubprogram(name: "operator()"{{.*}}flags: DIFlagPublic | DIFlagPrototyped, spFlags: DISPFlagLocalToUnit | DISPFlagOptimized)

// OGCG-DAG: distinct !DISubprogram(name: "operator()"{{.*}}DIFlagAllCallsDescribed{{.*}}DISPFlagDefinition
// OGCG-DAG: !DISubprogram(name: "operator()"{{.*}}flags: DIFlagPublic | DIFlagPrototyped, spFlags: DISPFlagLocalToUnit | DISPFlagOptimized)
