// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fclangir -emit-llvm -debug-info-kind=limited %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -emit-llvm -debug-info-kind=limited %s -o - | FileCheck %s --check-prefix=OGCG

// An anonymous union/struct nested in a record that is referenced only by a
// module-level global (no function in the TU) must carry DW_AT_export_symbols
// (DIFlagExportSymbols) in its debug info. The global record-type debug rewrite
// copied the CIR-level record flags, which omit it; it now recomputes the
// source-shaped record flags.

struct WithAnonU {
  int tag;
  union { int i; float f; };
};

struct WithAnonS {
  struct { int a; int b; };
  int c;
};

WithAnonU gu;
WithAnonS gs;

// CHECK-DAG: !DICompositeType(tag: DW_TAG_union_type{{.*}}flags: DIFlagExportSymbols
// CHECK-DAG: !DICompositeType(tag: DW_TAG_structure_type, file:{{.*}}flags: DIFlagExportSymbols

// OGCG-DAG: !DICompositeType(tag: DW_TAG_union_type{{.*}}flags: DIFlagExportSymbols
// OGCG-DAG: !DICompositeType(tag: DW_TAG_structure_type, scope:{{.*}}flags: DIFlagExportSymbols
