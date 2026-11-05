// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -debug-info-kind=limited -std=c++17 %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -debug-info-kind=limited -std=c++17 %s -o - | FileCheck %s --check-prefix=OGCG

// std::nullptr_t (decltype(nullptr)) lowers to a void pointer in CIR, but classic
// CodeGen describes it as a DW_TAG_unspecified_type named "decltype(nullptr)",
// not a void* pointer type. Verify a single unspecified type is emitted and is
// referenced from a global, a record member, a parameter, and a local — every
// position that previously got a spurious "void *".

decltype(nullptr) gv;

struct S {
  decltype(nullptr) m;
};
S gs;

void freeparam(decltype(nullptr) p) {
  decltype(nullptr) loc = nullptr;
  (void)p;
  (void)loc;
}

// A single unspecified type is shared by every reference.
// CHECK-DAG: ![[NP:[0-9]+]] = !DIBasicType(tag: DW_TAG_unspecified_type, name: "decltype(nullptr)")
// CHECK-DAG: !DIGlobalVariable(name: "gv",{{.*}}type: ![[NP]]
// CHECK-DAG: !DIDerivedType(tag: DW_TAG_member, name: "m",{{.*}}baseType: ![[NP]]
// CHECK-DAG: !DILocalVariable(name: "p", arg: 1,{{.*}}type: ![[NP]]
// CHECK-DAG: !DILocalVariable(name: "loc",{{.*}}type: ![[NP]]
// CHECK-NOT: !DIBasicType(name: "void", encoding: DW_ATE_address)

// OGCG-DAG: ![[ONP:[0-9]+]] = !DIBasicType(tag: DW_TAG_unspecified_type, name: "decltype(nullptr)")
// OGCG-DAG: !DIGlobalVariable(name: "gv",{{.*}}type: ![[ONP]]
// OGCG-DAG: !DIDerivedType(tag: DW_TAG_member, name: "m",{{.*}}baseType: ![[ONP]]
// OGCG-DAG: !DILocalVariable(name: "p", arg: 1,{{.*}}type: ![[ONP]]
// OGCG-DAG: !DILocalVariable(name: "loc",{{.*}}type: ![[ONP]]
