// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// A restrict-qualified pointer global keeps its DW_TAG_restrict_type wrapper.
// CIR pointer types don't carry the restrict qualifier, so it is reconstructed
// from the source declaration.

int *restrict grp;

// CIR: !DIGlobalVariable(name: "grp"{{.*}} type: ![[R:[0-9]+]]
// CIR: ![[R]] = !DIDerivedType(tag: DW_TAG_restrict_type, baseType: ![[P:[0-9]+]])
// CIR: ![[P]] = !DIDerivedType(tag: DW_TAG_pointer_type

// OGCG: !DIGlobalVariable(name: "grp"{{.*}} type: ![[R:[0-9]+]]
// OGCG: ![[R]] = !DIDerivedType(tag: DW_TAG_restrict_type, baseType: ![[P:[0-9]+]])
// OGCG: ![[P]] = !DIDerivedType(tag: DW_TAG_pointer_type
