// RUN: %clang_cc1 -triple aarch64-none-linux-android26 -std=gnu++20 -fclangir \
// RUN:   -emit-llvm -debug-info-kind=constructor %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -triple aarch64-none-linux-android26 -std=gnu++20 \
// RUN:   -emit-llvm -debug-info-kind=constructor %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

// Regression test: a function pointer type whose parameters include
// a struct caused CIR to emit a null entry in the DISubroutineType, which
// DWARF's constructSubprogramArguments asserted must only appear at the end
// (it interprets null as unspecified/variadic).

struct Size {
    int width;
    int height;
};

typedef void (*BinaryFunc)(const unsigned char* src1, unsigned long step1,
                           const unsigned char* src2, unsigned long step2,
                           unsigned char* dst, unsigned long step, Size sz,
                           void*);

static BinaryFunc cvtTab[8][8];

BinaryFunc getConvertFunc(int sdepth, int ddepth) {
    return cvtTab[sdepth][ddepth];
}

// Verify the global keeps the source-shaped BinaryFunc typedef, flattened array
// dimensions, nested const qualifiers, by-value Size fwd-decl, and `void *`
// null-pointee debug type.
// CHECK-DAG: !DIGlobalVariable(name: "cvtTab", linkageName: "_ZL6cvtTab", scope: ![[#]], file: ![[FILE:[0-9]+]], line: 23, type: ![[CVTTAB:[0-9]+]], isLocal: true, isDefinition: true)
// CHECK-DAG: ![[CVTTAB]] = !DICompositeType(tag: DW_TAG_array_type, baseType: ![[BINARY:[0-9]+]], size: 4096, elements: ![[DIMS:[0-9]+]])
// CHECK-DAG: ![[BINARY]] = !DIDerivedType(tag: DW_TAG_typedef, name: "BinaryFunc", file: ![[FILE]], line: 18, baseType: ![[FNPTR:[0-9]+]])
// CHECK-DAG: ![[FNPTR]] = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: ![[SIG:[0-9]+]], size: 64)
// CHECK-DAG: ![[SIG]] = !DISubroutineType(types: ![[SIGTYS:[0-9]+]])
// CHECK-DAG: ![[SIGTYS]] = !{null, ![[CONSTPTR:[0-9]+]], ![[ULONG:[0-9]+]], ![[CONSTPTR]], ![[ULONG]], ![[DSTPTR:[0-9]+]], ![[ULONG]], ![[SIZE:[0-9]+]], ![[VOIDPTR:[0-9]+]]}
// CHECK-DAG: ![[CONSTPTR]] = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: ![[CONSTU8:[0-9]+]], size: 64)
// CHECK-DAG: ![[CONSTU8]] = !DIDerivedType(tag: DW_TAG_const_type, baseType: ![[U8:[0-9]+]])
// CHECK-DAG: ![[U8]] = !DIBasicType(name: "unsigned char", size: 8, encoding: DW_ATE_unsigned_char)
// CHECK-DAG: ![[ULONG]] = !DIBasicType(name: "unsigned long", size: 64, encoding: DW_ATE_unsigned)
// CHECK-DAG: ![[DSTPTR]] = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: ![[U8]], size: 64)
// CHECK-DAG: ![[SIZE]] = {{(distinct )?}}!DICompositeType(tag: DW_TAG_structure_type, name: "Size", file: ![[FILE]], line: 13, size: 64, flags: DIFlagFwdDecl, identifier: "_ZTS4Size")
// CHECK-DAG: ![[VOIDPTR]] = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: null, size: 64)
// CHECK-DAG: ![[DIMS]] = !{![[DIM:[0-9]+]], ![[DIM]]}
// CHECK-DAG: ![[DIM]] = !DISubrange(count: 8)

// Verify the function return type also keeps the source-shaped BinaryFunc
// signature instead of dropping back to a raw function pointer subtree.
// CHECK-DAG: !DISubprogram(name: "getConvertFunc", linkageName: "_Z14getConvertFuncii", scope: ![[#]], file: ![[#]], line: 25, type: ![[GETFUNC:[0-9]+]], scopeLine: 25, flags: DIFlagPrototyped, spFlags: DISPFlagDefinition, unit: ![[#]], retainedNodes: ![[#]])
// CHECK-DAG: ![[GETFUNC]] = !DISubroutineType(types: ![[GETFUNCTYS:[0-9]+]])
// CHECK-DAG: ![[GETFUNCTYS]] = !{![[RETBINARY:[0-9]+]], ![[INT:[0-9]+]], ![[INT]]}
// CHECK-DAG: ![[RETBINARY]] = !DIDerivedType(tag: DW_TAG_typedef, name: "BinaryFunc", file: ![[FILE]], line: 18, baseType: ![[RETFNPTR:[0-9]+]])
// CHECK-DAG: ![[RETFNPTR]] = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: ![[RETSIG:[0-9]+]], size: 64)
// CHECK-DAG: ![[RETSIG]] = !DISubroutineType(types: ![[RETSIGTYS:[0-9]+]])
// CHECK-DAG: ![[RETSIGTYS]] = !{null, ![[CONSTPTR]], ![[ULONG]], ![[CONSTPTR]], ![[ULONG]], ![[DSTPTR]], ![[ULONG]], ![[RETSIZE:[0-9]+]], ![[VOIDPTR]]}
// CHECK-DAG: ![[RETSIZE]] = {{(distinct )?}}!DICompositeType(tag: DW_TAG_structure_type, name: "Size", file: ![[FILE]], line: 13, size: 64, flags: DIFlagFwdDecl, identifier: "_ZTS4Size")

// OGCG-DAG: !DIGlobalVariable(name: "cvtTab", linkageName: "_ZL6cvtTab", scope: ![[#]], file: ![[FILE:[0-9]+]], line: 23, type: ![[CVTTAB:[0-9]+]], isLocal: true, isDefinition: true)
// OGCG-DAG: ![[CVTTAB]] = !DICompositeType(tag: DW_TAG_array_type, baseType: ![[BINARY:[0-9]+]], size: 4096, elements: ![[DIMS:[0-9]+]])
// OGCG-DAG: ![[BINARY]] = !DIDerivedType(tag: DW_TAG_typedef, name: "BinaryFunc", file: ![[FILE]], line: 18, baseType: ![[FNPTR:[0-9]+]])
// OGCG-DAG: ![[FNPTR]] = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: ![[SIG:[0-9]+]], size: 64)
// OGCG-DAG: ![[SIG]] = !DISubroutineType(types: ![[SIGTYS:[0-9]+]])
// OGCG-DAG: ![[SIGTYS]] = !{null, ![[CONSTPTR:[0-9]+]], ![[ULONG:[0-9]+]], ![[CONSTPTR]], ![[ULONG]], ![[DSTPTR:[0-9]+]], ![[ULONG]], ![[SIZE:[0-9]+]], ![[VOIDPTR:[0-9]+]]}
// OGCG-DAG: ![[CONSTPTR]] = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: ![[CONSTU8:[0-9]+]], size: 64)
// OGCG-DAG: ![[CONSTU8]] = !DIDerivedType(tag: DW_TAG_const_type, baseType: ![[U8:[0-9]+]])
// OGCG-DAG: ![[U8]] = !DIBasicType(name: "unsigned char", size: 8, encoding: DW_ATE_unsigned_char)
// OGCG-DAG: ![[ULONG]] = !DIBasicType(name: "unsigned long", size: 64, encoding: DW_ATE_unsigned)
// OGCG-DAG: ![[DSTPTR]] = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: ![[U8]], size: 64)
// OGCG-DAG: ![[SIZE]] = {{(distinct )?}}!DICompositeType(tag: DW_TAG_structure_type, name: "Size", file: ![[FILE]], line: 13, size: 64, flags: DIFlagFwdDecl, identifier: "_ZTS4Size")
// OGCG-DAG: ![[VOIDPTR]] = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: null, size: 64)
// OGCG-DAG: ![[DIMS]] = !{![[DIM:[0-9]+]], ![[DIM]]}
// OGCG-DAG: ![[DIM]] = !DISubrange(count: 8)
// OGCG-DAG: !DISubprogram(name: "getConvertFunc", linkageName: "_Z14getConvertFuncii", scope: ![[#]], file: ![[#]], line: 25, type: ![[GETFUNC:[0-9]+]], scopeLine: 25, flags: DIFlagPrototyped, spFlags: DISPFlagDefinition, unit: ![[#]], retainedNodes: ![[#]])
// OGCG-DAG: ![[GETFUNC]] = !DISubroutineType(types: ![[GETFUNCTYS:[0-9]+]])
// OGCG-DAG: ![[GETFUNCTYS]] = !{![[BINARY]], ![[INT:[0-9]+]], ![[INT]]}
