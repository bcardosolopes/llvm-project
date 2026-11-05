// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -debug-info-kind=limited -std=c++17 %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -debug-info-kind=limited -std=c++17 %s -o - | FileCheck %s --check-prefix=OGCG

// A using-declaration of a (non-overloaded) function emits a
// DW_TAG_imported_declaration whose entity is the imported function's own DIE
// (its DISubprogram) with no name, matching classic CodeGen. CIR previously
// pointed the entity at the enclosing namespace and added a spurious name.

namespace N {
int fn(int x) { return x + 1; }
}

using N::fn;

int g() { return fn(5); }

// The imported declaration points at fn's DISubprogram (no name field; the
// entity ref comes right after scope).
// CHECK-DAG: ![[FN:[0-9]+]] = distinct !DISubprogram(name: "fn"
// CHECK-DAG: !DIImportedEntity(tag: DW_TAG_imported_declaration, scope: ![[#]], entity: ![[FN]], file: ![[#]], line: 13)

// OGCG-DAG: ![[OFN:[0-9]+]] = distinct !DISubprogram(name: "fn"
// OGCG-DAG: !DIImportedEntity(tag: DW_TAG_imported_declaration, scope: ![[#]], entity: ![[OFN]], file: ![[#]], line: 13)
