// RUN: %clang_cc1 -triple aarch64-none-linux-android24 -std=gnu11 \
// RUN:   -fclangir -emit-llvm -debug-info-kind=constructor %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple aarch64-none-linux-android24 -std=gnu11 \
// RUN:   -emit-llvm -debug-info-kind=constructor %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Regression: when multiple locals in the entry block are
// initialized from the same parameter-derived value, only one local may carry
// the debug argument number. Otherwise LLVM verifier reports
// "conflicting debug info for argument".
typedef struct S { int x; } *TT_CMap;
typedef struct S14 { int x; } *TT_CMap14;

int f(void *cmap) {
  TT_CMap ttcmap = (TT_CMap)cmap;
  TT_CMap14 cmap14 = (TT_CMap14)cmap;
  return (ttcmap != 0) + (cmap14 != 0);
}

// CIR: !DILocalVariable(name: "cmap", arg: 1
// CIR: !DILocalVariable(name: "ttcmap", scope:
// CIR-NOT: !DILocalVariable(name: "ttcmap", arg:
// CIR: !DILocalVariable(name: "cmap14", scope:
// CIR-NOT: !DILocalVariable(name: "cmap14", arg:

// OGCG: !DILocalVariable(name: "cmap", arg: 1
// OGCG: !DILocalVariable(name: "ttcmap", scope:
// OGCG-NOT: !DILocalVariable(name: "ttcmap", arg:
// OGCG: !DILocalVariable(name: "cmap14", scope:
// OGCG-NOT: !DILocalVariable(name: "cmap14", arg:
