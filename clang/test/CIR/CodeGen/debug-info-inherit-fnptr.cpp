// RUN: %clang_cc1 -triple aarch64-none-linux-android26 -fclangir -emit-llvm \
// RUN:   -std=gnu++20 -debug-info-kind=limited %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -triple aarch64-none-linux-android26 -fclangir -emit-obj \
// RUN:   -std=gnu++20 -debug-info-kind=limited %s -o /dev/null
//
// Verify that debug info for function pointer members in classes with base
// classes produces correct DISubroutineType entries (no null parameter types).
// CIR's lowered struct includes base subobject members that don't appear in
// RecordDecl::fields(), so the DI member matching must account for the offset
// difference.

typedef const struct __MCFType *MCFTypeRef;
typedef const struct __MCFDictionary *MCFDictionaryRef;
typedef int EventType;

typedef void (*TraceHandler)(
    MCFTypeRef ctx, EventType type,
    long long start, long long end, MCFDictionaryRef dict);

struct Base {
  virtual ~Base();
  int base_field;
};

struct Derived : Base {
  MCFTypeRef context;
  TraceHandler traceHandler;
};

void f(Derived *s) {
  s->traceHandler(s->context, 0, 0, 0, 0);
}

// Verify the Derived struct has correct field names and types.
// CHECK: !DICompositeType(tag: DW_TAG_structure_type, name: "Derived",
// CHECK-SAME: elements: ![[ELEMS:[0-9]+]])
// CHECK: ![[ELEMS]] = !{![[CTX:[0-9]+]], ![[TH:[0-9]+]]}
// CHECK: ![[CTX]] = !DIDerivedType(tag: DW_TAG_member, name: "context",
// CHECK: ![[TH]] = !DIDerivedType(tag: DW_TAG_member, name: "traceHandler",
// CHECK-SAME: baseType: ![[TH_TYPEDEF:[0-9]+]]
// CHECK: ![[TH_TYPEDEF]] = !DIDerivedType(tag: DW_TAG_typedef, name: "TraceHandler",
// CHECK-SAME: baseType: ![[TH_PTR:[0-9]+]]
// CHECK: ![[TH_PTR]] = !DIDerivedType(tag: DW_TAG_pointer_type,
// CHECK-SAME: baseType: ![[TH_SUBR:[0-9]+]]

// The TraceHandler subroutine type: void(MCFTypeRef, EventType, long long,
// long long, MCFDictionaryRef) — all 5 param types must be non-null.
// CHECK: ![[TH_SUBR]] = !DISubroutineType(types: ![[TH_TYPES:[0-9]+]])
// CHECK: ![[TH_TYPES]] = !{null, !{{[0-9]+}}, !{{[0-9]+}}, !{{[0-9]+}}, !{{[0-9]+}}, !{{[0-9]+}}}
