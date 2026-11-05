// RUN: %clang_cc1 -std=c++14 -emit-llvm -debug-info-kind=limited -triple x86_64-unknown-linux-gnu -fclangir %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -std=c++14 -emit-llvm -debug-info-kind=limited -triple x86_64-unknown-linux-gnu %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s -check-prefix=OGCG

// RUN: %clang_cc1 -std=c++14 -emit-llvm -debug-info-kind=limited -O2 -triple x86_64-unknown-linux-gnu -fclangir %s -o %t.cir.opt.ll
// RUN: FileCheck --input-file=%t.cir.opt.ll %s -check-prefix=OPT
// RUN: %clang_cc1 -std=c++14 -emit-llvm -debug-info-kind=limited -O2 -triple x86_64-unknown-linux-gnu %s -o %t.og.opt.ll
// RUN: FileCheck --input-file=%t.og.opt.ll %s -check-prefix=OGCG-OPT

int main() {}

// C++14 language, producer present, isOptimized false, FullDebug.
// LLVM: distinct !DICompileUnit(language: DW_LANG_C_plus_plus_14, file: !{{[0-9]+}}, producer: "{{.*}}clang{{.*}}", isOptimized: false, runtimeVersion: 0, emissionKind: FullDebug, splitDebugInlining: false, nameTableKind: None)
// OGCG: distinct !DICompileUnit(language: DW_LANG_C_plus_plus_14, file: !{{[0-9]+}}, producer: "{{.*}}clang{{.*}}", isOptimized: false, runtimeVersion: 0, emissionKind: FullDebug

// C++14 with -O2: isOptimized true.
// OPT: distinct !DICompileUnit(language: DW_LANG_C_plus_plus_14, file: !{{[0-9]+}}, producer: "{{.*}}clang{{.*}}", isOptimized: true, runtimeVersion: 0, emissionKind: FullDebug, splitDebugInlining: false, nameTableKind: None)
// OGCG-OPT: distinct !DICompileUnit(language: DW_LANG_C_plus_plus_14, file: !{{[0-9]+}}, producer: "{{.*}}clang{{.*}}", isOptimized: true, runtimeVersion: 0, emissionKind: FullDebug
