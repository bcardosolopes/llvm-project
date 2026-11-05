// RUN: %clang_cc1 -triple aarch64-none-linux-android26 -std=gnu11 -fclangir \
// RUN:   -emit-llvm -debug-info-kind=constructor %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -triple aarch64-none-linux-android26 -std=gnu11 \
// RUN:   -emit-llvm -debug-info-kind=constructor %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

// Regression test: a function pointer type whose parameters include _Float16
// originally crashed because CIR inserted a mid-signature null DI entry.
// The remaining parity requirement is that direct-CIR preserve the source
// callback_t/fp16_t typedef wrappers and OGCG's null-pointee void* shape.

typedef _Float16 fp16_t;
typedef void (*callback_t)(void *, fp16_t, int);

struct S {
  callback_t cb;
  int x;
};

struct S g;

callback_t getCallback(void) {
  return g.cb;
}

// CHECK: !DIDerivedType(tag: DW_TAG_member, name: "cb", scope: ![[STRUCT:[0-9]+]], file: ![[FILE:[0-9]+]], line: 17, baseType: ![[CALLBACK:[0-9]+]], size: 64)
// CHECK: ![[CALLBACK]] = !DIDerivedType(tag: DW_TAG_typedef, name: "callback_t", file: ![[FILE]], line: 14, baseType: ![[CALLBACK_PTR:[0-9]+]])
// CHECK: ![[CALLBACK_PTR]] = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: ![[CALLBACK_SUBTY:[0-9]+]], size: 64)
// CHECK: ![[CALLBACK_SUBTY]] = !DISubroutineType(types: ![[CALLBACK_TYPES:[0-9]+]])
// CHECK: ![[CALLBACK_TYPES]] = !{null, ![[VOID_PTR:[0-9]+]], ![[FP16:[0-9]+]], ![[INT:[0-9]+]]}
// CHECK: ![[VOID_PTR]] = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: null, size: 64)
// CHECK: ![[FP16]] = !DIDerivedType(tag: DW_TAG_typedef, name: "fp16_t", file: ![[FILE]], line: 13, baseType: ![[HALF:[0-9]+]])
// CHECK: ![[HALF]] = !DIBasicType(name: "_Float16", size: 16, encoding: DW_ATE_float)
// CHECK: ![[INT]] = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
// CHECK: ![[GETCALLBACK_SP:[0-9]+]] = distinct !DISubprogram(name: "getCallback"{{.*}}type: ![[GETCALLBACK_SIG:[0-9]+]]
// CHECK: ![[GETCALLBACK_SIG]] = !DISubroutineType(types: ![[GETCALLBACK_TYPES:[0-9]+]])
// CHECK: ![[GETCALLBACK_TYPES]] = !{![[CALLBACK]]}

// OGCG: !DIDerivedType(tag: DW_TAG_member, name: "cb", scope: ![[STRUCT:[0-9]+]], file: ![[FILE:[0-9]+]], line: 17, baseType: ![[CALLBACK:[0-9]+]], size: 64)
// OGCG: ![[CALLBACK]] = !DIDerivedType(tag: DW_TAG_typedef, name: "callback_t", file: ![[FILE]], line: 14, baseType: ![[CALLBACK_PTR:[0-9]+]])
// OGCG: ![[CALLBACK_PTR]] = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: ![[CALLBACK_SUBTY:[0-9]+]], size: 64)
// OGCG: ![[CALLBACK_SUBTY]] = !DISubroutineType(types: ![[CALLBACK_TYPES:[0-9]+]])
// OGCG: ![[CALLBACK_TYPES]] = !{null, ![[VOID_PTR:[0-9]+]], ![[FP16:[0-9]+]], ![[INT:[0-9]+]]}
// OGCG: ![[VOID_PTR]] = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: null, size: 64)
// OGCG: ![[FP16]] = !DIDerivedType(tag: DW_TAG_typedef, name: "fp16_t", file: ![[FILE]], line: 13, baseType: ![[HALF:[0-9]+]])
// OGCG: ![[HALF]] = !DIBasicType(name: "_Float16", size: 16, encoding: DW_ATE_float)
// OGCG: ![[INT]] = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
// OGCG: ![[GETCALLBACK_SP:[0-9]+]] = distinct !DISubprogram(name: "getCallback"{{.*}}type: ![[GETCALLBACK_SIG:[0-9]+]]
// OGCG: ![[GETCALLBACK_SIG]] = !DISubroutineType(types: ![[GETCALLBACK_TYPES:[0-9]+]])
// OGCG: ![[GETCALLBACK_TYPES]] = !{![[CALLBACK]]}
