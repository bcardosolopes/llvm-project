// RUN: %clang_cc1 -fclangir -triple x86_64-unknown-linux -emit-llvm -debug-info-kind=limited -std=c++17 -o - %s | FileCheck %s
// RUN: %clang_cc1 -fclangir -triple x86_64-unknown-linux -emit-llvm -debug-info-kind=limited -std=c++17 -o - %s | llvm-as -o /dev/null

// Verify that declaration-only member function DISubprograms are added
// to the record type's elements, even for methods that are not called
// in the current TU. This matches OGCG behavior.

struct Widget {
  int x;
  int getX() const { return x; }
  void setX(int v) { x = v; }
};

void use() {
  Widget w;
  w.setX(42);
}

// The Widget type used as setX's scope should have both getX and setX
// as declaration-only member subprograms.
// CHECK-DAG: !DICompositeType(tag: DW_TAG_structure_type, name: "Widget", {{.*}}elements: ![[ELEMS:[0-9]+]]
// CHECK-DAG: !DISubprogram(name: "getX", linkageName: "_ZNK6Widget4getXEv", scope: !{{[0-9]+}}, {{.*}}spFlags: 0)
// CHECK-DAG: !DISubprogram(name: "setX", linkageName: "_ZN6Widget4setXEi", scope: !{{[0-9]+}}, {{.*}}spFlags: 0)
