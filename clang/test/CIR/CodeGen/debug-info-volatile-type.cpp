// RUN: %clang_cc1 -fclangir -triple x86_64-unknown-linux -emit-llvm -debug-info-kind=limited -o - %s | FileCheck %s
// RUN: %clang_cc1 -fclangir -triple x86_64-unknown-linux -emit-llvm -debug-info-kind=limited -o - %s | llvm-as -o /dev/null

// Verify that volatile-qualified types produce DW_TAG_volatile_type in debug
// info, matching OGCG.

void test_volatile_int() {
  volatile int x = 42;
}

void test_const_volatile_int() {
  const volatile int y = 10;
}

// volatile int should have DW_TAG_volatile_type wrapping int.
// CHECK-DAG: !DILocalVariable(name: "x", {{.*}}type: ![[VINT:[0-9]+]])
// CHECK-DAG: ![[VINT]] = !DIDerivedType(tag: DW_TAG_volatile_type, baseType: ![[INT:[0-9]+]])
// CHECK-DAG: ![[INT]] = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)

// const volatile int should have const(volatile(int)).
// CHECK-DAG: !DILocalVariable(name: "y", {{.*}}type: ![[CVINT:[0-9]+]])
// CHECK-DAG: ![[CVINT]] = !DIDerivedType(tag: DW_TAG_const_type, baseType: ![[VINT]])
