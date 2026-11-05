// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -debug-info-kind=limited -std=c++17 %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -debug-info-kind=limited -std=c++17 %s -o - | FileCheck %s --check-prefix=OGCG

// The implicit object parameter ('this') is compiler-synthesized, so its
// DILocalVariable must NOT carry a DW_AT_decl_file / DW_AT_decl_line: classic
// CodeGen omits them (the parameter has no source spelling). CIR previously
// stamped a spurious file/line on the 'this' variable during lowering; this
// only affects the implicit object parameter, not an explicit object parameter.

struct S {
  int m;
  int get() { return m; }
};

int useS(S *p) { return p->get(); }

// The 'this' variable goes straight from scope to type, with no file:/line:
// fields in between (matching classic CodeGen).
// CHECK: !DILocalVariable(name: "this", arg: 1, scope: ![[#]], type: ![[#]], flags: DIFlagArtificial | DIFlagObjectPointer)
// OGCG: !DILocalVariable(name: "this", arg: 1, scope: ![[#]], type: ![[#]], flags: DIFlagArtificial | DIFlagObjectPointer)
