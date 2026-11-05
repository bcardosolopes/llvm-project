// RUN: %clang_cc1 -triple aarch64-none-linux-android26 -std=gnu++20 \
// RUN:   -fclangir -emit-llvm -debug-info-kind=limited %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple aarch64-none-linux-android26 -std=gnu++20 \
// RUN:   -emit-llvm -debug-info-kind=limited %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Regression: do not assign function-argument arg numbers to
// local lambda captures/variables (can cause verifier failure:
// "conflicting debug info for argument").
struct S {
  void append(const char *);
};
const char *toStrU(unsigned);
const char *toStrI(int);

void f() {
  S typeString;
  const auto appendStr = [&](const char *s) { typeString.append(s); };
  const auto appendUint = [&](unsigned u) { typeString.append(toStrU(u)); };
  const auto appendInt = [&](int i) { typeString.append(toStrI(i)); };
  appendStr("x");
  appendUint(1u);
  appendInt(-1);
}

// CIR-DAG: ![[CIR_FUNC:[0-9]+]] = distinct !DISubprogram(name: "f", linkageName: "_Z1fv", scope: !{{[0-9]+}}, file: !{{[0-9]+}}, line: 17, type: !{{[0-9]+}}, scopeLine: 17, flags: DIFlagPrototyped, spFlags: DISPFlagDefinition, unit: !{{[0-9]+}}, retainedNodes: !{{[0-9]+}})
// CIR-DAG: ![[CIR_S_TYPE:[0-9]+]] = distinct !DICompositeType(tag: DW_TAG_structure_type, name: "S", file: !{{[0-9]+}}, line: 11, size: 8, flags: DIFlagTypePassByValue, elements: !{{[0-9]+}}, identifier: "_ZTS1S")
// CIR-DAG: ![[CIR_CLOSURE0:[0-9]+]] = distinct !DICompositeType(tag: DW_TAG_class_type, scope: ![[CIR_FUNC]], file: !{{[0-9]+}}, line: 19, size: 64, flags: DIFlagTypePassByValue | DIFlagNonTrivial, elements: ![[CIR_CLOSURE0_MEMBERS:[0-9]+]])
// CIR-DAG: ![[CIR_CLOSURE0_MEMBERS]] = !{![[CIR_CLOSURE0_FIELD:[0-9]+]]}
// CIR-DAG: ![[CIR_CLOSURE0_FIELD]] = !DIDerivedType(tag: DW_TAG_member, name: "typeString", scope: ![[CIR_CLOSURE0]], file: !{{[0-9]+}}, line: 19, baseType: ![[CIR_CLOSURE0_FIELD_TY:[0-9]+]], size: 64)
// CIR-DAG: ![[CIR_CLOSURE0_FIELD_TY]] = !DIDerivedType(tag: DW_TAG_reference_type, baseType: ![[CIR_S_TYPE]], size: 64)
// CIR-DAG: ![[CIR_DECL0:[0-9]+]] = !DISubprogram(name: "operator()", scope: ![[CIR_CLOSURE0]], file: !{{[0-9]+}}, line: 19, type: !{{[0-9]+}}, scopeLine: 19, flags: DIFlagPublic | DIFlagPrototyped, spFlags: DISPFlagLocalToUnit)
// CIR-DAG: ![[CIR_SP0:[0-9]+]] = distinct !DISubprogram(name: "operator()", linkageName: "_ZZ1fvENK3$_0clEPKc", scope: ![[CIR_CLOSURE0]], file: !{{[0-9]+}}, line: 19, type: !{{[0-9]+}}, scopeLine: 19, flags: DIFlagPrototyped, spFlags: DISPFlagLocalToUnit | DISPFlagDefinition, unit: !{{[0-9]+}}, declaration: ![[CIR_DECL0]], retainedNodes: !{{[0-9]+}})
// CIR-DAG: !DILocalVariable(name: "this", arg: 1, scope: ![[CIR_SP0]], type: ![[CIR_THIS0_TY:[0-9]+]], flags: DIFlagArtificial | DIFlagObjectPointer)
// CIR-DAG: ![[CIR_THIS0_TY]] = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: ![[CIR_THIS0_BASE:[0-9]+]], size: 64)
// CIR-DAG: ![[CIR_THIS0_BASE]] = !DIDerivedType(tag: DW_TAG_const_type, baseType: ![[CIR_CLOSURE0]])
// CIR-DAG: !DILocalVariable(name: "typeString", scope: ![[CIR_FUNC]], file: !{{[0-9]+}}, line: 18, type: ![[CIR_S_TYPE]])
// CIR-DAG: !DILocalVariable(name: "appendStr", scope: ![[CIR_FUNC]], file: !{{[0-9]+}}, line: 19, type: ![[CIR_THIS0_BASE]])
// CIR-DAG: !DILocalVariable(name: "appendUint", scope: ![[CIR_FUNC]], file: !{{[0-9]+}}, line: 20, type: ![[CIR_APPENDUINT_TY:[0-9]+]])
// CIR-DAG: ![[CIR_APPENDUINT_TY]] = !DIDerivedType(tag: DW_TAG_const_type, baseType: ![[CIR_CLOSURE1:[0-9]+]])
// CIR-DAG: ![[CIR_CLOSURE1]] = distinct !DICompositeType(tag: DW_TAG_class_type, scope: ![[CIR_FUNC]], file: !{{[0-9]+}}, line: 20, size: 64, flags: DIFlagTypePassByValue | DIFlagNonTrivial, elements: !{{[0-9]+}})
// CIR-DAG: !DILocalVariable(name: "appendInt", scope: ![[CIR_FUNC]], file: !{{[0-9]+}}, line: 21, type: ![[CIR_APPENDINT_TY:[0-9]+]])
// CIR-DAG: ![[CIR_APPENDINT_TY]] = !DIDerivedType(tag: DW_TAG_const_type, baseType: ![[CIR_CLOSURE2:[0-9]+]])
// CIR-DAG: ![[CIR_CLOSURE2]] = distinct !DICompositeType(tag: DW_TAG_class_type, scope: ![[CIR_FUNC]], file: !{{[0-9]+}}, line: 21, size: 64, flags: DIFlagTypePassByValue | DIFlagNonTrivial, elements: !{{[0-9]+}})

// OGCG-DAG: ![[OG_CLOSURE0:[0-9]+]] = distinct !DICompositeType(tag: DW_TAG_class_type, scope: !{{[0-9]+}}, file: !{{[0-9]+}}, line: 19, size: 64, flags: DIFlagTypePassByValue | DIFlagNonTrivial, elements: !{{[0-9]+}})
// OGCG-DAG: ![[OG_DECL0:[0-9]+]] = !DISubprogram(name: "operator()", scope: ![[OG_CLOSURE0]], file: !{{[0-9]+}}, line: 19, type: !{{[0-9]+}}, scopeLine: 19, flags: DIFlagPublic | DIFlagPrototyped, spFlags: DISPFlagLocalToUnit)
// OGCG-DAG: ![[OG_SP0:[0-9]+]] = distinct !DISubprogram(name: "operator()", linkageName: "_ZZ1fvENK3$_0clEPKc", scope: ![[OG_CLOSURE0]], file: !{{[0-9]+}}, line: 19, type: !{{[0-9]+}}, scopeLine: 19, flags: DIFlagPrototyped, spFlags: DISPFlagLocalToUnit | DISPFlagDefinition, unit: !{{[0-9]+}}, declaration: ![[OG_DECL0]], retainedNodes: !{{[0-9]+}})
// OGCG-DAG: !DILocalVariable(name: "this", arg: 1, scope: ![[OG_SP0]], type: !{{[0-9]+}}, flags: DIFlagArtificial | DIFlagObjectPointer)
// OGCG-DAG: !DILocalVariable(name: "appendStr", scope:
// OGCG-DAG: !DILocalVariable(name: "appendUint", scope:
// OGCG-DAG: !DILocalVariable(name: "appendInt", scope:
