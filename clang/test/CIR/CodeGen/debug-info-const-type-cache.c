// RUN: %clang_cc1 -triple aarch64-none-linux-android24 -Oz \
// RUN:   -fclangir -fclangir-mem2reg -emit-obj \
// RUN:   -gkey-instructions -debug-info-kind=constructor -dwarf-version=4 \
// RUN:   %s -o /dev/null
//
// RUN: %clang_cc1 -triple aarch64-none-linux-android24 -Oz \
// RUN:   -fclangir -fclangir-mem2reg -emit-llvm -disable-llvm-passes \
// RUN:   -gkey-instructions -debug-info-kind=constructor -dwarf-version=4 \
// RUN:   %s -o %t.ll
// RUN: FileCheck %s < %t.ll

// Verify that the debug type cache does not incorrectly apply const wrapping
// to non-const allocas that share the same element type as a const alloca.
// Previously, const wrapping was cached alongside the base type, causing
// "conflicting debug info for argument" verifier failures when a const
// parameter preceded a non-const parameter of the same type.

void use(int *);

void func(const int p, int x) {
  use(&x);
}

// 'x' should get plain int, 'p' should get const int — not both const.
// CHECK-DAG: !DILocalVariable(name: "x", {{.*}}type: [[PLAIN_INT:![0-9]+]])
// CHECK-DAG: !DILocalVariable(name: "p", {{.*}}type: [[CONST_INT:![0-9]+]])
// CHECK-DAG: [[PLAIN_INT]] = !DIBasicType(name: "int"
// CHECK-DAG: [[CONST_INT]] = !DIDerivedType(tag: DW_TAG_const_type, baseType: [[PLAIN_INT]])
