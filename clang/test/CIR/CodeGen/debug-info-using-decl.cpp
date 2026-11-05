// RUN: %clang_cc1 -fclangir -triple x86_64-unknown-linux -emit-llvm -debug-info-kind=limited -std=c++17 -o - %s | FileCheck %s
// RUN: %clang_cc1 -fclangir -triple x86_64-unknown-linux -emit-llvm -debug-info-kind=limited -std=c++17 -o - %s | llvm-as -o /dev/null

// Verify that using declarations emit DW_TAG_imported_module and
// DW_TAG_imported_declaration in CIR debug info, matching OGCG behavior.

namespace ns {
  int value = 42;
}

using namespace ns;

int foo() {
  return value;
}

// The compile unit should have an imports list.
// CHECK-DAG: !DICompileUnit({{.*}}imports: ![[IMPORTS:[0-9]+]]

// File-scope "using namespace ns" should emit an imported_module.
// CHECK-DAG: !DIImportedEntity(tag: DW_TAG_imported_module, scope: !{{[0-9]+}}, entity: ![[NS:[0-9]+]], file: !{{[0-9]+}}, line: {{[0-9]+}})
// CHECK-DAG: ![[NS]] = !DINamespace(name: "ns", scope: null)
