// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -debug-info-kind=limited -std=c++17 %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -debug-info-kind=limited -std=c++17 %s -o - | FileCheck %s --check-prefix=OGCG

// A using-declaration of a variable ('using N::v;') emits a
// DW_TAG_imported_declaration whose entity is the imported variable's own DIE
// (its DIGlobalVariable) with no name, matching classic CodeGen. CIR previously
// pointed the entity at the enclosing namespace and added a spurious name.

namespace N {
int v = 5;
}

using N::v;

int g() { return v; }

// The imported declaration points at v's DIGlobalVariable (no name field; the
// entity ref comes right after scope).
// CHECK-DAG: ![[V:[0-9]+]] = distinct !DIGlobalVariable(name: "v"
// CHECK-DAG: !DIImportedEntity(tag: DW_TAG_imported_declaration, scope: ![[#]], entity: ![[V]], file: ![[#]], line: 13)

// OGCG-DAG: ![[OV:[0-9]+]] = distinct !DIGlobalVariable(name: "v"
// OGCG-DAG: !DIImportedEntity(tag: DW_TAG_imported_declaration, scope: ![[#]], entity: ![[OV]], file: ![[#]], line: 13)
