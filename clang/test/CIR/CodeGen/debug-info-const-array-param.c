// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// A `const`-element array parameter adjusts to a pointer-to-const (here
// `const int *`). Its const must survive in debug info. CIR keeps the source
// array type (`const int[4]`) for the parameter's debug info but does not carry
// the element cv-qualifier in its own types, so the const was dropped (the
// parameter came out as a plain `int *`). The array element's const qualifier
// now triggers source-shaped debug-info repair, matching classic CodeGen.

int sum4(const int a[4]) { return a[0] + a[1] + a[2] + a[3]; }

// CIR-DAG: !DIDerivedType(tag: DW_TAG_pointer_type, baseType: ![[CI:[0-9]+]]
// CIR-DAG: ![[CI]] = !DIDerivedType(tag: DW_TAG_const_type, baseType: ![[I:[0-9]+]])
// CIR-DAG: ![[I]] = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)

// OGCG-DAG: !DIDerivedType(tag: DW_TAG_pointer_type, baseType: ![[OCI:[0-9]+]]
// OGCG-DAG: ![[OCI]] = !DIDerivedType(tag: DW_TAG_const_type, baseType: ![[OI:[0-9]+]])
// OGCG-DAG: ![[OI]] = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
