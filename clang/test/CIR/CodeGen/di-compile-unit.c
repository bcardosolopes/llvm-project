// RUN: %clang_cc1 -emit-llvm -debug-info-kind=limited -triple x86_64-unknown-linux-gnu -fclangir %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -emit-llvm -debug-info-kind=limited -triple x86_64-unknown-linux-gnu %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s -check-prefix=OGCG

// RUN: %clang_cc1 -Qn -emit-llvm -debug-info-kind=limited -triple x86_64-unknown-linux-gnu -fclangir %s -o %t.cir.qn.ll
// RUN: FileCheck --input-file=%t.cir.qn.ll %s -check-prefix=QN
// RUN: %clang_cc1 -Qn -emit-llvm -debug-info-kind=limited -triple x86_64-unknown-linux-gnu %s -o %t.og.qn.ll
// RUN: FileCheck --input-file=%t.og.qn.ll %s -check-prefix=OGCG-QN

// RUN: %clang_cc1 -emit-llvm -debug-info-kind=line-tables-only -triple x86_64-unknown-linux-gnu -fclangir %s -o %t.cir.lt.ll
// RUN: FileCheck --input-file=%t.cir.lt.ll %s -check-prefix=LT
// RUN: %clang_cc1 -emit-llvm -debug-info-kind=line-tables-only -triple x86_64-unknown-linux-gnu %s -o %t.og.lt.ll
// RUN: FileCheck --input-file=%t.og.lt.ll %s -check-prefix=OGCG-LT

int main(void) {}

// Default: C11 language, producer present, isOptimized false, FullDebug.
// LLVM: distinct !DICompileUnit(language: DW_LANG_C11, file: !{{[0-9]+}}, producer: "{{.*}}clang{{.*}}", isOptimized: false, runtimeVersion: 0, emissionKind: FullDebug, splitDebugInlining: false, nameTableKind: None)
// OGCG: distinct !DICompileUnit(language: DW_LANG_C11, file: !{{[0-9]+}}, producer: "{{.*}}clang{{.*}}", isOptimized: false, runtimeVersion: 0, emissionKind: FullDebug

// -Qn: No producer in DICompileUnit, no llvm.ident.
// QN-NOT: llvm.ident
// QN: distinct !DICompileUnit(language: DW_LANG_C11, file: !{{[0-9]+}}, isOptimized: false, runtimeVersion: 0, emissionKind: FullDebug, splitDebugInlining: false, nameTableKind: None)
// QN-NOT: producer:

// OGCG-QN-NOT: llvm.ident
// OGCG-QN: distinct !DICompileUnit(language: DW_LANG_C11, file: !{{[0-9]+}}, isOptimized: false, runtimeVersion: 0, emissionKind: FullDebug

// Line-tables-only emission kind.
// LT: distinct !DICompileUnit(language: DW_LANG_C11, file: !{{[0-9]+}}, producer: "{{.*}}clang{{.*}}", isOptimized: false, runtimeVersion: 0, emissionKind: LineTablesOnly, splitDebugInlining: false, nameTableKind: None)
// OGCG-LT: distinct !DICompileUnit(language: DW_LANG_C11, file: !{{[0-9]+}}, producer: "{{.*}}clang{{.*}}", isOptimized: false, runtimeVersion: 0, emissionKind: LineTablesOnly
