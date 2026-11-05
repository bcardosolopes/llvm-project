// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Ensure CIR preserves signed/unsigned integer debug type names/encodings for
// source globals, matching OGCG.

signed char sc = -1;
unsigned char uc = 255;
short ss = -2;
unsigned short us = 3;

// CIR-DAG: !DIBasicType(name: "signed char", size: 8, encoding: DW_ATE_signed_char)
// CIR-DAG: !DIBasicType(name: "unsigned char", size: 8, encoding: DW_ATE_unsigned_char)
// CIR-DAG: !DIBasicType(name: "short", size: 16, encoding: DW_ATE_signed)
// CIR-DAG: !DIBasicType(name: "unsigned short", size: 16, encoding: DW_ATE_unsigned)

// OGCG-DAG: !DIBasicType(name: "signed char", size: 8, encoding: DW_ATE_signed_char)
// OGCG-DAG: !DIBasicType(name: "unsigned char", size: 8, encoding: DW_ATE_unsigned_char)
// OGCG-DAG: !DIBasicType(name: "short", size: 16, encoding: DW_ATE_signed)
// OGCG-DAG: !DIBasicType(name: "unsigned short", size: 16, encoding: DW_ATE_unsigned)
