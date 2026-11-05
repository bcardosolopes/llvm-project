// RUN: %clang_cc1 -std=gnu++20 -triple x86_64-unknown-linux-gnu -emit-llvm -debug-info-kind=limited %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s
// RUN: %clang_cc1 -std=gnu++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -debug-info-kind=limited %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s
//
//
//
// Former divergence: global debug type naming/encoding for language character
// types. Direct-CIR now matches OGCG's wchar_t/char16_t/char32_t metadata.

wchar_t w = L'a';
char16_t c16 = u'a';
char32_t c32 = U'a';

// CHECK-DAG: !DIBasicType(name: "char16_t", size: 16, encoding: DW_ATE_UTF)
// CHECK-DAG: !DIBasicType(name: "char32_t", size: 32, encoding: DW_ATE_UTF)
// CHECK-DAG: !DIBasicType(name: "wchar_t", size: 32, encoding: DW_ATE_signed)
// CHECK-DAG: !DIGlobalVariable(name: "w", scope: {{.*}}, file: {{.*}}, line: 11, type: !{{[0-9]+}}
// CHECK-DAG: !DIGlobalVariable(name: "c16", scope: {{.*}}, file: {{.*}}, line: 12, type: !{{[0-9]+}}
// CHECK-DAG: !DIGlobalVariable(name: "c32", scope: {{.*}}, file: {{.*}}, line: 13, type: !{{[0-9]+}}
