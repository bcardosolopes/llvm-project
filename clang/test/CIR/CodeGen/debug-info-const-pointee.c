// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// A const-qualified pointer pointee (e.g. `const int *`) must keep its
// DW_TAG_const_type in debug info. CIR pointer types do not carry pointee
// cv-qualifiers, and the const pointee was dropped (the type came out as a plain
// `int *`) for parameters and locals, while volatile pointees and const record
// pointees were preserved. The pointee's const qualifier now triggers the
// source-shaped debug-info repair, matching classic CodeGen.

int load_ci(const int *p) { return *p; }
char load_cc(const char *p) { return *p; }

// CIR-DAG: !DIDerivedType(tag: DW_TAG_pointer_type, baseType: ![[CI:[0-9]+]]
// CIR-DAG: ![[CI]] = !DIDerivedType(tag: DW_TAG_const_type, baseType: ![[I:[0-9]+]])
// CIR-DAG: ![[I]] = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)

// OGCG-DAG: !DIDerivedType(tag: DW_TAG_pointer_type, baseType: ![[OCI:[0-9]+]]
// OGCG-DAG: ![[OCI]] = !DIDerivedType(tag: DW_TAG_const_type, baseType: ![[OI:[0-9]+]])
// OGCG-DAG: ![[OI]] = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
