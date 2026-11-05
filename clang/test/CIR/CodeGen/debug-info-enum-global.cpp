// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -debug-info-kind=limited -std=c++17 %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -debug-info-kind=limited -std=c++17 %s -o - | FileCheck %s --check-prefix=OGCG

// An enum referenced only by a global variable is reconstructed from
// source-level attributes carried out of CIRGen. That path dropped the ODR
// type identifier for every enum and the DIFlagEnumClass flag for scoped enums,
// so a global-only `enum class` was indistinguishable from an unscoped enum in
// debug info (no DW_AT_enum_class) and lost its uniquing identifier. Both are
// now forwarded from CIRGen, matching classic CodeGen.

enum E { A = -5, B = 1000000 };
E ge = A;

enum class EC : long { X = 1 };
EC gec = EC::X;

// CHECK-DAG: !DICompositeType(tag: DW_TAG_enumeration_type, name: "E"{{.*}}, identifier: "_ZTS1E")
// CHECK-DAG: !DICompositeType(tag: DW_TAG_enumeration_type, name: "EC"{{.*}}, flags: DIFlagEnumClass{{.*}}, identifier: "_ZTS2EC")

// OGCG-DAG: !DICompositeType(tag: DW_TAG_enumeration_type, name: "E"{{.*}}, identifier: "_ZTS1E")
// OGCG-DAG: !DICompositeType(tag: DW_TAG_enumeration_type, name: "EC"{{.*}}, flags: DIFlagEnumClass{{.*}}, identifier: "_ZTS2EC")
