// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -O2 \
// RUN:   -gkey-instructions -debug-info-kind=line-tables-only %s -o %t.og.ll
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -O2 \
// RUN:   -gkey-instructions -debug-info-kind=line-tables-only %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.og.ll --check-prefix=NO-LINKAGE %s
// RUN: FileCheck --input-file=%t.cir.ll --check-prefix=NO-LINKAGE %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -O2 \
// RUN:   -fdebug-info-for-profiling -debug-info-kind=line-tables-only %s -o %t.prof.og.ll
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -O2 \
// RUN:   -fdebug-info-for-profiling -debug-info-kind=line-tables-only %s -o %t.prof.cir.ll
// RUN: FileCheck --input-file=%t.prof.og.ll --check-prefix=KEEP-LINKAGE %s
// RUN: FileCheck --input-file=%t.prof.cir.ll --check-prefix=KEEP-LINKAGE %s

#line 1 "debug-info-subprogram-linkagename-line-tables-only.cpp"
int g;
void f() {}
int h() { return g = 1; }

// NO-LINKAGE: ![[F:[0-9]+]] = distinct !DISubprogram(name: "f", scope: ![[FILE:[0-9]+]], file: ![[FILE]], line: 2, type: ![[F_TY:[0-9]+]], scopeLine: 2, flags: DIFlagPrototyped, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: ![[CU:[0-9]+]], keyInstructions: true)
// NO-LINKAGE: ![[H:[0-9]+]] = distinct !DISubprogram(name: "h", scope: ![[FILE]], file: ![[FILE]], line: 3, type: ![[H_TY:[0-9]+]], scopeLine: 3, flags: DIFlagPrototyped, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: ![[CU]], keyInstructions: true)

// KEEP-LINKAGE: ![[PF:[0-9]+]] = distinct !DISubprogram(name: "f", linkageName: "_Z1fv", scope: ![[PFILE:[0-9]+]], file: ![[PFILE]], line: 2, type: ![[PF_TY:[0-9]+]], scopeLine: 2, flags: DIFlagPrototyped, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: ![[PCU:[0-9]+]])
// KEEP-LINKAGE: ![[PH:[0-9]+]] = distinct !DISubprogram(name: "h", linkageName: "_Z1hv", scope: ![[PFILE]], file: ![[PFILE]], line: 3, type: ![[PH_TY:[0-9]+]], scopeLine: 3, flags: DIFlagPrototyped, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: ![[PCU]])
