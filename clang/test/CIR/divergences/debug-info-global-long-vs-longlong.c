// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -debug-info-kind=limited %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -debug-info-kind=limited %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s
//
//
//
// Former divergence: global integer debug type naming for 64-bit fundamental
// integers. Direct-CIR now distinguishes long from long long like OGCG.

long lv = 1;
unsigned long ulv = 2;
long long llv = 3;
unsigned long long ullv = 4;

// CHECK-DAG: !DIBasicType(name: "unsigned long", size: 64, encoding: DW_ATE_unsigned)
// CHECK-DAG: !DIBasicType(name: "long long", size: 64, encoding: DW_ATE_signed)
// CHECK-DAG: !DIBasicType(name: "unsigned long long", size: 64, encoding: DW_ATE_unsigned)
// CHECK-DAG: !DIBasicType(name: "long", size: 64, encoding: DW_ATE_signed)
// CHECK-DAG: !DIGlobalVariable(name: "lv", scope: {{.*}}, file: {{.*}}, line: 11, type: !{{[0-9]+}}
// CHECK-DAG: !DIGlobalVariable(name: "ulv", scope: {{.*}}, file: {{.*}}, line: 12, type: !{{[0-9]+}}
// CHECK-DAG: !DIGlobalVariable(name: "llv", scope: {{.*}}, file: {{.*}}, line: 13, type: !{{[0-9]+}}
// CHECK-DAG: !DIGlobalVariable(name: "ullv", scope: {{.*}}, file: {{.*}}, line: 14, type: !{{[0-9]+}}
