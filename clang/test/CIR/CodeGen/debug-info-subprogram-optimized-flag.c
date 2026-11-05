// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o - | FileCheck %s --check-prefix=CIR-O0 --implicit-check-not='declare !dbg' --implicit-check-not='!DISubprogram(name: "ext"'
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -debug-info-kind=limited -O2 %s -o - | FileCheck %s --check-prefix=CIR-O2 --implicit-check-not='declare !dbg' --implicit-check-not='!DISubprogram(name: "ext"'
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o - | FileCheck %s --check-prefix=OGCG-O0 --implicit-check-not='declare !dbg' --implicit-check-not='!DISubprogram(name: "ext"'
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm \
// RUN:   -debug-info-kind=limited -O2 %s -o - | FileCheck %s --check-prefix=OGCG-O2 --implicit-check-not='declare !dbg' --implicit-check-not='!DISubprogram(name: "ext"'

extern int ext(int *p);

int foo(int *p) {
  return *p;
}

int bar(int *p) {
  return ext(p);
}

// CIR-O0: declare i32 @ext(ptr noundef)
// CIR-O0: ![[FOO:[0-9]+]] = distinct !DISubprogram(name: "foo"
// CIR-O0-SAME: flags: DIFlagPrototyped,
// CIR-O0-SAME: spFlags: DISPFlagDefinition,
// CIR-O0-SAME: retainedNodes: ![[#]]
// CIR-O0: ![[BAR:[0-9]+]] = distinct !DISubprogram(name: "bar"
// CIR-O0-SAME: flags: DIFlagPrototyped,
// CIR-O0-SAME: spFlags: DISPFlagDefinition,
// CIR-O0-SAME: retainedNodes: ![[#]]
// CIR-O0-NOT: linkageName: "foo"
// CIR-O0-NOT: linkageName: "bar"

// OGCG-O0: declare i32 @ext(ptr noundef)
// OGCG-O0: ![[FOO:[0-9]+]] = distinct !DISubprogram(name: "foo"
// OGCG-O0-SAME: flags: DIFlagPrototyped,
// OGCG-O0-SAME: spFlags: DISPFlagDefinition,
// OGCG-O0-SAME: retainedNodes: ![[#]]
// OGCG-O0: ![[BAR:[0-9]+]] = distinct !DISubprogram(name: "bar"
// OGCG-O0-SAME: flags: DIFlagPrototyped,
// OGCG-O0-SAME: spFlags: DISPFlagDefinition,
// OGCG-O0-SAME: retainedNodes: ![[#]]
// OGCG-O0-NOT: linkageName: "foo"
// OGCG-O0-NOT: linkageName: "bar"

// CIR-O2: declare i32 @ext(ptr noundef)
// CIR-O2: ![[FOO:[0-9]+]] = distinct !DISubprogram(name: "foo"
// CIR-O2-SAME: flags: DIFlagPrototyped,
// CIR-O2-SAME: spFlags: DISPFlagDefinition | DISPFlagOptimized,
// CIR-O2-SAME: retainedNodes: ![[FOO_RETAIN:[0-9]+]]
// CIR-O2-DAG: ![[FOO_RETAIN]] = !{![[FOO_P:[0-9]+]]}
// CIR-O2-DAG: ![[FOO_P]] = !DILocalVariable(name: "p", arg: 1, scope: ![[FOO]], file: ![[#]], line: [[#]], type: ![[#]])
// CIR-O2: ![[BAR:[0-9]+]] = distinct !DISubprogram(name: "bar"
// CIR-O2-SAME: flags: DIFlagPrototyped,
// CIR-O2-SAME: spFlags: DISPFlagDefinition | DISPFlagOptimized,
// CIR-O2-SAME: retainedNodes: ![[BAR_RETAIN:[0-9]+]]
// CIR-O2-DAG: ![[BAR_RETAIN]] = !{![[BAR_P:[0-9]+]]}
// CIR-O2-DAG: ![[BAR_P]] = !DILocalVariable(name: "p", arg: 1, scope: ![[BAR]], file: ![[#]], line: [[#]], type: ![[#]])
// CIR-O2-NOT: linkageName: "foo"
// CIR-O2-NOT: linkageName: "bar"

// OGCG-O2: declare i32 @ext(ptr noundef)
// OGCG-O2: ![[FOO:[0-9]+]] = distinct !DISubprogram(name: "foo"
// OGCG-O2-SAME: flags: DIFlagPrototyped,
// OGCG-O2-SAME: spFlags: DISPFlagDefinition | DISPFlagOptimized,
// OGCG-O2-SAME: retainedNodes: ![[FOO_RETAIN:[0-9]+]]
// OGCG-O2-DAG: ![[FOO_RETAIN]] = !{![[FOO_P:[0-9]+]]}
// OGCG-O2-DAG: ![[FOO_P]] = !DILocalVariable(name: "p", arg: 1, scope: ![[FOO]], file: ![[#]], line: [[#]], type: ![[#]])
// OGCG-O2: ![[BAR:[0-9]+]] = distinct !DISubprogram(name: "bar"
// OGCG-O2-SAME: flags: DIFlagPrototyped,
// OGCG-O2-SAME: spFlags: DISPFlagDefinition | DISPFlagOptimized,
// OGCG-O2-SAME: retainedNodes: ![[BAR_RETAIN:[0-9]+]]
// OGCG-O2-DAG: ![[BAR_RETAIN]] = !{![[BAR_P:[0-9]+]]}
// OGCG-O2-DAG: ![[BAR_P]] = !DILocalVariable(name: "p", arg: 1, scope: ![[BAR]], file: ![[#]], line: [[#]], type: ![[#]])
// OGCG-O2-NOT: linkageName: "foo"
// OGCG-O2-NOT: linkageName: "bar"
