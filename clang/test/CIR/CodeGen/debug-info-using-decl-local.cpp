// RUN: %clang_cc1 -fclangir -triple x86_64-unknown-linux -emit-llvm -debug-info-kind=limited -std=c++17 -o - %s | FileCheck %s
// RUN: %clang_cc1 -fclangir -triple x86_64-unknown-linux -emit-llvm -debug-info-kind=limited -std=c++17 -o - %s | llvm-as -o /dev/null

// Verify that function-local using declarations are attached to the enclosing
// DISubprogram's retainedNodes, not the DICompileUnit's imports list. LLVM's
// DwarfDebug asserts that CU imports never carry a DILocalScope.

namespace N { int x; }

void foo() {
  using N::x;
  x = 42;
}

// The namespace should exist.
// CHECK-DAG: ![[NS:[0-9]+]] = !DINamespace(name: "N", scope: null)

// The imported declaration should be scoped to the DISubprogram, not the CU,
// and point at the imported variable's own DIE (no name), matching classic.
// CHECK-DAG: ![[X:[0-9]+]] = distinct !DIGlobalVariable(name: "x"
// CHECK-DAG: !DISubprogram(name: "foo",{{.*}}retainedNodes: ![[RETAINED:[0-9]+]]
// CHECK-DAG: ![[RETAINED]] = !{![[IE:[0-9]+]]}
// CHECK-DAG: ![[IE]] = !DIImportedEntity(tag: DW_TAG_imported_declaration, scope: !{{[0-9]+}}, entity: ![[X]]

// The CU should NOT have an imports list containing function-local entities.
// CHECK-NOT: !DICompileUnit({{.*}}imports:
