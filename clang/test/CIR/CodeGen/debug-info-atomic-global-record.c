// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -debug-info-kind=limited -std=c2x %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -debug-info-kind=limited -std=c2x %s -o - | FileCheck %s --check-prefix=OGCG

// A record reached only through an _Atomic-qualified module-level global must
// have its members' source scope/file/line rebuilt, like a plain global. The
// gate that decides whether a global needs source-shaped record debug info did
// not unwrap _Atomic, so the global was skipped and its members were emitted
// without DW_AT_decl_file / DW_AT_decl_line (and without the enclosing scope).

struct AtomWrap {
  int x;
  int y;
};
_Atomic struct AtomWrap gaw;

// The members carry their source file + line (and the record is wrapped in a
// DW_TAG_atomic_type), matching classic.
// CHECK-DAG: !DICompositeType(tag: DW_TAG_structure_type, name: "AtomWrap"
// CHECK-DAG: !DIDerivedType(tag: DW_TAG_member, name: "x", scope: ![[#]], file: ![[#]], line: 11
// CHECK-DAG: !DIDerivedType(tag: DW_TAG_member, name: "y", scope: ![[#]], file: ![[#]], line: 12
// CHECK-DAG: !DIDerivedType(tag: DW_TAG_atomic_type

// OGCG-DAG: !DICompositeType(tag: DW_TAG_structure_type, name: "AtomWrap"
// OGCG-DAG: !DIDerivedType(tag: DW_TAG_member, name: "x", scope: ![[#]], file: ![[#]], line: 11
// OGCG-DAG: !DIDerivedType(tag: DW_TAG_member, name: "y", scope: ![[#]], file: ![[#]], line: 12
// OGCG-DAG: !DIDerivedType(tag: DW_TAG_atomic_type
