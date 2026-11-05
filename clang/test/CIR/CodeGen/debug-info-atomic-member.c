// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -debug-info-kind=limited %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -debug-info-kind=limited %s -o - | FileCheck %s --check-prefix=OGCG

// An _Atomic-qualified record member must keep the DW_TAG_atomic_type wrapper in
// debug info. _Atomic strips to its value type in CIR, so the wrapper is rebuilt
// at lowering time from the AST member type; the reconstruction lacked an
// AtomicType case for both the subprogram-driven and global record paths.

struct S {
  _Atomic int a;
  int b;
};

// Referenced by a function (subprogram record-DI path).
int use(struct S *s) { return s->b; }

// Referenced by a global (global record-DI path).
struct S gs;

// CHECK: !DIDerivedType(tag: DW_TAG_member, name: "a"{{.*}}baseType: ![[ATOMIC:[0-9]+]]
// CHECK: ![[ATOMIC]] = !DIDerivedType(tag: DW_TAG_atomic_type, baseType: ![[INT:[0-9]+]]
// CHECK: ![[INT]] = !DIBasicType(name: "int"

// OGCG: !DIDerivedType(tag: DW_TAG_member, name: "a"{{.*}}baseType: ![[ATOMIC:[0-9]+]]
// OGCG: ![[ATOMIC]] = !DIDerivedType(tag: DW_TAG_atomic_type
