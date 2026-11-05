// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir --check-prefix=CIR %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

// Test that __attribute__((section)) on static local variables is forwarded.

int get_value(void) {
  static int x __attribute__((section(".mysection"))) = 42;
  return x;
}

// CIR: cir.global "private" internal {{.*}}@get_value.x = #cir.int<42> : !s32i {alignment = 4 : i64, section = ".mysection"}

// LLVM: @get_value.x = internal global i32 42, section ".mysection", align 4
// OGCG: @get_value.x = internal global i32 42, section ".mysection", align 4
