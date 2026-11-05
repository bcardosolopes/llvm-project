// RUN: %clang_cc1 -fclangir -triple x86_64-unknown-linux -emit-llvm -debug-info-kind=limited -O2 -dwarf-version=5 -o - %s | FileCheck %s
// RUN: %clang_cc1 -fclangir -triple x86_64-unknown-linux -emit-llvm -debug-info-kind=limited -O2 -dwarf-version=5 -o - %s | llvm-as -o /dev/null

// Verify that CIR creates declaration DISubprograms for external functions
// called from optimized code (needed for DW_TAG_call_site emission).

extern int bar(int x);

int foo(int a) {
  return bar(a + 1);
}

// foo should have AllCallsDescribed flag.
// CHECK-DAG: distinct !DISubprogram(name: "foo", {{.*}}flags: {{.*}}DIFlagAllCallsDescribed

// bar should have a declaration-only DISubprogram.
// CHECK-DAG: !DISubprogram(name: "bar", {{.*}}flags: DIFlagPrototyped, spFlags: DISPFlagOptimized)
