// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// A variadic function's subroutine type must end with a null entry, which DWARF
// renders as DW_TAG_unspecified_parameters (the '...'). A non-variadic function
// must not have that trailing null.

int variadic(int n, ...) { return n; }
int fixed(int n) { return n; }

// CIR: !DISubprogram(name: "variadic"{{.*}} type: ![[VT:[0-9]+]]
// CIR: ![[VT]] = !DISubroutineType(types: ![[VL:[0-9]+]])
// CIR: ![[VL]] = !{![[I:[0-9]+]], ![[I]], null}
// CIR: !DISubprogram(name: "fixed"{{.*}} type: ![[FT:[0-9]+]]
// CIR: ![[FT]] = !DISubroutineType(types: ![[FL:[0-9]+]])
// CIR: ![[FL]] = !{![[I]], ![[I]]}

// OGCG: ![[VL:[0-9]+]] = !{![[I:[0-9]+]], ![[I]], null}
// OGCG: ![[FL:[0-9]+]] = !{![[I]], ![[I]]}
