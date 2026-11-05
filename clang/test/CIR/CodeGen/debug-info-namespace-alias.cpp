// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -debug-info-kind=limited -std=c++17 %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -debug-info-kind=limited -std=c++17 %s -o - | FileCheck %s --check-prefix=OGCG

// A namespace alias ('namespace Alias = N;') must be emitted as a
// DW_TAG_imported_declaration named after the alias and pointing at the aliased
// namespace, matching classic CodeGen. CIR handled using-declarations and
// using-directives but dropped namespace aliases entirely.

namespace N {
int v = 5;
}

namespace Alias = N;

int g() { return Alias::v; }

// CHECK: !DIImportedEntity(tag: DW_TAG_imported_declaration, name: "Alias", scope: ![[#]], entity: ![[#]], file: ![[#]], line: 13)
// OGCG: !DIImportedEntity(tag: DW_TAG_imported_declaration, name: "Alias", scope: ![[#]], entity: ![[#]], file: ![[#]], line: 13)
