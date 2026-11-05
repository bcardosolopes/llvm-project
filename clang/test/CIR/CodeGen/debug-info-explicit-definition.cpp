// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -debug-info-kind=limited -std=c++17 %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -debug-info-kind=limited -std=c++17 %s -o - | FileCheck %s --check-prefix=OGCG

// DIFlagExplicit describes the *declaration* of an explicit constructor or
// conversion operator; classic CodeGen omits it from the defining
// DISubprogram. CIR previously copied the explicit flag onto the definition
// too, emitting a spurious DW_AT_explicit on the concrete subprogram.

struct E {
  int m;
  explicit E(int);
  explicit operator bool() const;
};

E::E(int v) : m(v) {}
E::operator bool() const { return m != 0; }

int use() { E e(1); return (bool)e ? e.m : 0; }

// The declarations carry DIFlagExplicit.
// CHECK-DAG: !DISubprogram(name: "E",{{.*}}flags: DIFlagExplicit{{.*}}, spFlags: 0
// The definitions do NOT (flags start at DIFlagPrototyped, no Explicit).
// CHECK-DAG: distinct !DISubprogram(name: "E",{{.*}}flags: DIFlagPrototyped, spFlags: {{[A-Za-z| ]*}}DISPFlagDefinition

// OGCG-DAG: !DISubprogram(name: "E",{{.*}}flags: DIFlagExplicit{{.*}}, spFlags: 0
// OGCG-DAG: distinct !DISubprogram(name: "E",{{.*}}flags: DIFlagPrototyped, spFlags: {{[A-Za-z| ]*}}DISPFlagDefinition
