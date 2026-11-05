// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// A global with an explicit over-alignment must record DW_AT_alignment (the
// DIGlobalVariable "align" field, in bits). A plainly-aligned global records no
// "align" field (getMaxAlignment() is 0), so its DIGlobalVariable line ends at
// isDefinition without an align operand.

_Alignas(64) int galigned;
__attribute__((aligned(32))) double galignedd;
int gnormal;

// CIR-DAG: !DIGlobalVariable(name: "galigned"{{.*}} isDefinition: true, align: 512)
// CIR-DAG: !DIGlobalVariable(name: "galignedd"{{.*}} isDefinition: true, align: 256)
// CIR-DAG: !DIGlobalVariable(name: "gnormal"{{.*}} isDefinition: true)

// OGCG-DAG: !DIGlobalVariable(name: "galigned"{{.*}} isDefinition: true, align: 512)
// OGCG-DAG: !DIGlobalVariable(name: "galignedd"{{.*}} isDefinition: true, align: 256)
// OGCG-DAG: !DIGlobalVariable(name: "gnormal"{{.*}} isDefinition: true)
