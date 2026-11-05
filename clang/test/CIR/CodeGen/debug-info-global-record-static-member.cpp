// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -debug-info-kind=limited -std=c++17 %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -debug-info-kind=limited -std=c++17 %s -o - | FileCheck %s --check-prefix=OGCG

// Static data members of a record referenced only by a module-level global (no
// function in the TU) must be emitted as DW_TAG_member with DIFlagStaticMember,
// matching classic CodeGen. The CIR global record-type debug rewrite only
// walked instance fields (recordDef->fields() excludes statics), so static
// members were dropped entirely. The access specifier (relative to the record's
// default) and, for constexpr/const members, the initializer value in extraData
// must also match classic.

struct S {           // struct: default public
  static int plain;
  static constexpr int cx = 42;

private:
  static double priv;
};
int S::plain = 0;
double S::priv = 0;

class K {            // class: default private
  static int dflt;

public:
  static constexpr long pub = 7;
};
int K::dflt = 0;

S gs;
K gk;

// CHECK-DAG: !DIDerivedType(tag: DW_TAG_member, name: "plain",{{.*}}flags: DIFlagStaticMember)
// CHECK-DAG: !DIDerivedType(tag: DW_TAG_member, name: "cx",{{.*}}flags: DIFlagStaticMember, extraData: i32 42)
// CHECK-DAG: !DIDerivedType(tag: DW_TAG_member, name: "priv",{{.*}}flags: DIFlagPrivate | DIFlagStaticMember)
// CHECK-DAG: !DIDerivedType(tag: DW_TAG_member, name: "dflt",{{.*}}flags: DIFlagStaticMember)
// CHECK-DAG: !DIDerivedType(tag: DW_TAG_member, name: "pub",{{.*}}flags: DIFlagPublic | DIFlagStaticMember, extraData: i64 7)

// OGCG-DAG: !DIDerivedType(tag: DW_TAG_member, name: "plain",{{.*}}flags: DIFlagStaticMember)
// OGCG-DAG: !DIDerivedType(tag: DW_TAG_member, name: "cx",{{.*}}flags: DIFlagStaticMember, extraData: i32 42)
// OGCG-DAG: !DIDerivedType(tag: DW_TAG_member, name: "priv",{{.*}}flags: DIFlagPrivate | DIFlagStaticMember)
// OGCG-DAG: !DIDerivedType(tag: DW_TAG_member, name: "dflt",{{.*}}flags: DIFlagStaticMember)
// OGCG-DAG: !DIDerivedType(tag: DW_TAG_member, name: "pub",{{.*}}flags: DIFlagPublic | DIFlagStaticMember, extraData: i64 7)
