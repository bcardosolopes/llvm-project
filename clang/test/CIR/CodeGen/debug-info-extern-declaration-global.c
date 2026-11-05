// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -debug-info-kind=limited %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -debug-info-kind=limited %s -o - | FileCheck %s --check-prefix=OGCG

// An external declaration that is referenced (but not defined) in this TU emits
// an `external global` with no definition here. Classic CodeGen attaches no
// DIGlobalVariable to such a declaration. CIR previously emitted a spurious
// DIGlobalVariableExpression (isDefinition: true) and a !dbg attachment on the
// `external global`. Tentative definitions and real definitions must still get
// debug info.

extern int ext;
int *use = &ext;

int tentative;
int defined = 7;

// The external declaration carries no debug attachment.
// CHECK: @ext = external global i32, align 4{{$}}
// CHECK-NOT: !DIGlobalVariable(name: "ext"
// CHECK-DAG: !DIGlobalVariable(name: "use"
// CHECK-DAG: !DIGlobalVariable(name: "tentative"
// CHECK-DAG: !DIGlobalVariable(name: "defined"

// OGCG: @ext = external global i32, align 4{{$}}
// OGCG-NOT: !DIGlobalVariable(name: "ext"
// OGCG-DAG: !DIGlobalVariable(name: "use"
// OGCG-DAG: !DIGlobalVariable(name: "tentative"
// OGCG-DAG: !DIGlobalVariable(name: "defined"
