// RUN: %clang_cc1 -triple aarch64-none-linux-android24 -fclangir -emit-llvm \
// RUN:   -std=gnu++20 -debug-info-kind=limited %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -triple aarch64-none-linux-android24 -fclangir -emit-obj \
// RUN:   -std=gnu++20 -debug-info-kind=limited %s -o /dev/null
//
// Verify that debug info for global variables of derived class types with
// function pointer members produces correct field entries. CIR's lowered
// struct includes base subobject members that don't appear in the AST's
// RecordDecl::fields(), so the global DI type rewriter must match by offset
// rather than sequential index.

typedef void (*FnPtr)(int);

struct Base {
  virtual ~Base();
  int x;
};

struct Derived : Base {
  FnPtr handler;
  int y;
};

Derived g;
void use() { g.handler(g.y); }

// Verify Derived struct has correct field names and types.
// CHECK: !DICompositeType(tag: DW_TAG_structure_type, name: "Derived",
// CHECK-SAME: elements: ![[ELEMS:[0-9]+]], identifier: "_ZTS7Derived")
// CHECK: ![[ELEMS]] = !{![[HANDLER:[0-9]+]], ![[Y:[0-9]+]]}
// CHECK: ![[HANDLER]] = !DIDerivedType(tag: DW_TAG_member, name: "handler",
// CHECK-SAME: offset: 128)
// CHECK: ![[Y]] = !DIDerivedType(tag: DW_TAG_member, name: "y",
// CHECK-SAME: offset: 192)
