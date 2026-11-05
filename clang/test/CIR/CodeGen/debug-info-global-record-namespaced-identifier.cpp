// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -debug-info-kind=limited %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -debug-info-kind=limited %s -o - | FileCheck %s --check-prefix=OGCG

// A record that is only referenced by a module-level global (no function in the
// TU) must carry the same DWARF scope and mangled type identifier as classic
// CodeGen. The global record-type debug rewrite previously copied the (empty)
// CIR-level identifier, dropping the `_ZTS...` identifier that backs DWARF type
// uniquing/ODR; and CIRGen sets the enclosing scope on struct composites but not
// on union composites, so a namespace-scoped union also lost its DINamespace
// scope.

namespace NS {
struct Inner {
  int a;
};
union U {
  int i;
  float f;
};
} // namespace NS

NS::Inner g_inner = {1};
NS::U g_u = {2};

// Both the struct and the union carry their DINamespace scope and the mangled
// identifier, byte-matching classic.
// CHECK-DAG: !DICompositeType(tag: DW_TAG_structure_type, name: "Inner", scope: ![[NS:[0-9]+]]{{.*}}identifier: "_ZTSN2NS5InnerE"
// CHECK-DAG: !DICompositeType(tag: DW_TAG_union_type, name: "U", scope: ![[NS]]{{.*}}identifier: "_ZTSN2NS1UE"
// CHECK-DAG: ![[NS]] = !DINamespace(name: "NS"

// OGCG-DAG: !DICompositeType(tag: DW_TAG_structure_type, name: "Inner", scope: ![[NS:[0-9]+]]{{.*}}identifier: "_ZTSN2NS5InnerE"
// OGCG-DAG: !DICompositeType(tag: DW_TAG_union_type, name: "U", scope: ![[NS]]{{.*}}identifier: "_ZTSN2NS1UE"
// OGCG-DAG: ![[NS]] = !DINamespace(name: "NS"
