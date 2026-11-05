// REQUIRES: x86-registered-target
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -debug-info-kind=limited -O2 %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm \
// RUN:   -debug-info-kind=limited -O2 %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG
// RUN: %clang -target x86_64-unknown-linux-gnu -g -O2 -S -emit-llvm -fclangir \
// RUN:   %s -o %t.driver.cir.ll
// RUN: FileCheck --input-file=%t.driver.cir.ll %s --check-prefix=DRIVER
// RUN: %clang -target x86_64-unknown-linux-gnu -g -O2 -S -emit-llvm \
// RUN:   %s -o %t.driver.ogcg.ll
// RUN: FileCheck --input-file=%t.driver.ogcg.ll %s --check-prefix=DRIVER
// RUN: %clang -target x86_64-unknown-linux-gnu -g -gdwarf-4 -O2 -S -emit-llvm \
// RUN:   -fclangir %s -o %t.driver.dwarf4.cir.ll
// RUN: FileCheck --input-file=%t.driver.dwarf4.cir.ll %s --check-prefix=DRIVER-DWARF4
// RUN: %clang -target x86_64-unknown-linux-gnu -g -gdwarf-4 -O2 -S -emit-llvm \
// RUN:   %s -o %t.driver.dwarf4.ogcg.ll
// RUN: FileCheck --input-file=%t.driver.dwarf4.ogcg.ll %s --check-prefix=DRIVER-DWARF4
// RUN: %clang -target x86_64-unknown-linux-gnu -g -gdwarf64 -O2 -S -emit-llvm \
// RUN:   -fclangir %s -o %t.driver.dwarf64.cir.ll
// RUN: FileCheck --input-file=%t.driver.dwarf64.cir.ll %s --check-prefix=DRIVER-DWARF64
// RUN: %clang -target x86_64-unknown-linux-gnu -g -gdwarf64 -O2 -S -emit-llvm \
// RUN:   %s -o %t.driver.dwarf64.ogcg.ll
// RUN: FileCheck --input-file=%t.driver.dwarf64.ogcg.ll %s --check-prefix=DRIVER-DWARF64

#define __tag1 __attribute__((btf_decl_tag("tag1")))
#define __tag2 __attribute__((btf_decl_tag("tag2")))

struct t1 {
  int a;
};

int foo(struct t1 __tag1 __tag2 *arg) {
  return arg->a;
}

// CIR: ![[SP:[0-9]+]] = distinct !DISubprogram(name: "foo", scope: ![[#]], file: ![[#]], line: [[#]], type: ![[#]], scopeLine: [[#]], flags: DIFlagPrototyped, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: ![[#]], retainedNodes: ![[RET:[0-9]+]])
// CIR: ![[RET]] = !{![[ARG:[0-9]+]]}
// CIR: ![[ARG]] = !DILocalVariable(name: "arg", arg: 1, scope: ![[#]], file: ![[#]], line: [[#]], type: ![[#]], annotations: ![[ANNOT:[0-9]+]])
// CIR-DAG: ![[ANNOT]] = !{![[TAG1:[0-9]+]], ![[TAG2:[0-9]+]]}
// CIR-DAG: ![[TAG1]] = !{!"btf_decl_tag", !"tag1"}
// CIR-DAG: ![[TAG2]] = !{!"btf_decl_tag", !"tag2"}

// OGCG: ![[SP:[0-9]+]] = distinct !DISubprogram(name: "foo", scope: ![[#]], file: ![[#]], line: [[#]], type: ![[#]], scopeLine: [[#]], flags: DIFlagPrototyped, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: ![[#]], retainedNodes: ![[RET:[0-9]+]])
// OGCG: ![[RET]] = !{![[ARG:[0-9]+]]}
// OGCG: ![[ARG]] = !DILocalVariable(name: "arg", arg: 1, scope: ![[#]], file: ![[#]], line: [[#]], type: ![[#]], annotations: ![[ANNOT:[0-9]+]])
// OGCG-DAG: ![[ANNOT]] = !{![[TAG1:[0-9]+]], ![[TAG2:[0-9]+]]}
// OGCG-DAG: ![[TAG1]] = !{!"btf_decl_tag", !"tag1"}
// OGCG-DAG: ![[TAG2]] = !{!"btf_decl_tag", !"tag2"}

// DRIVER: ![[DWARF_VERSION:[0-9]+]] = !{i32 7, !"Dwarf Version", i32 5}
// DRIVER: ![[SP:[0-9]+]] = distinct !DISubprogram(name: "foo", scope: ![[#]], file: ![[#]], line: [[#]], type: ![[#]], scopeLine: [[#]], flags: DIFlagPrototyped | DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: ![[#]], retainedNodes: ![[RET:[0-9]+]], keyInstructions: true)

// DRIVER-DWARF4: ![[DWARF_VERSION:[0-9]+]] = !{i32 7, !"Dwarf Version", i32 4}
// DRIVER-DWARF64: ![[DWARF64:[0-9]+]] = !{i32 7, !"DWARF64", i32 1}
