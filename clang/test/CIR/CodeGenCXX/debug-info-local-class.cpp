// RUN: %clang_cc1 -fclangir -triple x86_64-unknown-linux -emit-llvm -debug-info-kind=limited -o - %s | FileCheck %s
// RUN: %clang_cc1 -fclangir -triple x86_64-unknown-linux -emit-llvm -debug-info-kind=limited -o - %s | llvm-as -o /dev/null

// Verify that function-local classes produce a single DICompositeType scoped to
// the enclosing function's DISubprogram, matching OGCG.

void outer() {
  struct Local { int m(int x) { return x; } };
  Local l;
  l.m(1);
}

// There should be exactly one DICompositeType named "Local", scoped to outer().
// CHECK: distinct !DICompositeType(tag: DW_TAG_structure_type, name: "Local", scope: [[OUTER:![0-9]+]],
// CHECK-NOT: !DICompositeType({{.*}}name: "Local"

// The enclosing function is outer().
// CHECK-DAG: [[OUTER]] = distinct !DISubprogram(name: "outer",
