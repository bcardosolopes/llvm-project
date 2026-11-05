// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -debug-info-kind=limited %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -debug-info-kind=limited %s -o - | FileCheck %s --check-prefix=OGCG

// A namespace-scoped enum member of a record that is only referenced by a
// module-level global (no function in the TU) must carry the same DWARF scope
// and mangled type identifier as classic CodeGen. The global record-type debug
// rewrite previously hardcoded a null scope and empty identifier, so the enum
// diverged from classic (which emits the DINamespace scope and `_ZTS...`
// identifier used for DWARF type uniquing/ODR).

namespace N {
enum class Color { Red, Green, Blue };
struct Wrap {
  Color c;
  int x;
};
} // namespace N

N::Wrap g_wrap = {N::Color::Green, 42};

// CHECK-DAG: !DIDerivedType(tag: DW_TAG_member, name: "c"{{.*}}baseType: ![[ENUM:[0-9]+]]
// CHECK-DAG: ![[ENUM]] = !DICompositeType(tag: DW_TAG_enumeration_type, name: "Color", scope: ![[NS:[0-9]+]]{{.*}}flags: DIFlagEnumClass{{.*}}identifier: "_ZTSN1N5ColorE"
// CHECK-DAG: ![[NS]] = !DINamespace(name: "N"

// OGCG-DAG: !DICompositeType(tag: DW_TAG_enumeration_type, name: "Color", scope: ![[NS:[0-9]+]]{{.*}}identifier: "_ZTSN1N5ColorE"
// OGCG-DAG: ![[NS]] = !DINamespace(name: "N"
