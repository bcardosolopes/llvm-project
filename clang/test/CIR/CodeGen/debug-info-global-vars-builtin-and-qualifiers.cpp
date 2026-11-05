// RUN: %clang_cc1 -std=gnu++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -std=gnu++20 -triple x86_64-unknown-linux-gnu -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

bool gb = true;
wchar_t gw = L'a';
char16_t gc16 = u'a';
char32_t gc32 = U'a';
const int gci = 1;
volatile int gvi = 2;
const volatile int gcvi = 3;
char gc = 4;
signed char gsc = 5;
unsigned char guc = 6;
long gl = 7;
unsigned long gul = 8;
long long gll = 9;
unsigned long long gull = 10;

// CIR-DAG: !DIBasicType(name: "bool", size: 8, encoding: DW_ATE_boolean)
// CIR-DAG: !DIBasicType(name: "wchar_t", size: 32, encoding: DW_ATE_signed)
// CIR-DAG: !DIBasicType(name: "char16_t", size: 16, encoding: DW_ATE_UTF)
// CIR-DAG: !DIBasicType(name: "char32_t", size: 32, encoding: DW_ATE_UTF)
// CIR-DAG: !DIBasicType(name: "char", size: 8, encoding: DW_ATE_signed_char)
// CIR-DAG: !DIBasicType(name: "signed char", size: 8, encoding: DW_ATE_signed_char)
// CIR-DAG: !DIBasicType(name: "unsigned char", size: 8, encoding: DW_ATE_unsigned_char)
// CIR-DAG: !DIBasicType(name: "long", size: 64, encoding: DW_ATE_signed)
// CIR-DAG: !DIBasicType(name: "unsigned long", size: 64, encoding: DW_ATE_unsigned)
// CIR-DAG: !DIBasicType(name: "long long", size: 64, encoding: DW_ATE_signed)
// CIR-DAG: !DIBasicType(name: "unsigned long long", size: 64, encoding: DW_ATE_unsigned)
// CIR-DAG: !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
// CIR-DAG: !DIDerivedType(tag: DW_TAG_volatile_type, baseType: ![[#]])
// CIR-DAG: !DIDerivedType(tag: DW_TAG_const_type, baseType: ![[#]])

// OGCG-DAG: !DIBasicType(name: "bool", size: 8, encoding: DW_ATE_boolean)
// OGCG-DAG: !DIBasicType(name: "wchar_t", size: 32, encoding: DW_ATE_signed)
// OGCG-DAG: !DIBasicType(name: "char16_t", size: 16, encoding: DW_ATE_UTF)
// OGCG-DAG: !DIBasicType(name: "char32_t", size: 32, encoding: DW_ATE_UTF)
// OGCG-DAG: !DIBasicType(name: "char", size: 8, encoding: DW_ATE_signed_char)
// OGCG-DAG: !DIBasicType(name: "signed char", size: 8, encoding: DW_ATE_signed_char)
// OGCG-DAG: !DIBasicType(name: "unsigned char", size: 8, encoding: DW_ATE_unsigned_char)
// OGCG-DAG: !DIBasicType(name: "long", size: 64, encoding: DW_ATE_signed)
// OGCG-DAG: !DIBasicType(name: "unsigned long", size: 64, encoding: DW_ATE_unsigned)
// OGCG-DAG: !DIBasicType(name: "long long", size: 64, encoding: DW_ATE_signed)
// OGCG-DAG: !DIBasicType(name: "unsigned long long", size: 64, encoding: DW_ATE_unsigned)
// OGCG-DAG: !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
// OGCG-DAG: !DIDerivedType(tag: DW_TAG_volatile_type, baseType: ![[#]])
// OGCG-DAG: !DIDerivedType(tag: DW_TAG_const_type, baseType: ![[#]])
