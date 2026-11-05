// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t-cir.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ll %s

// Match OGCG for wide string literals that initialize a smaller static-local
// array through an initializer list: keep only the destination element count
// instead of asserting on the dropped null terminator.

void f(void) {
  static int c[1] = {L"a"};
}

// CIR: cir.global "private" {{.*}} @f.c = #cir.const_array<[#cir.int<97> : !s32i]> : !cir.array<!s32i x 1>
// LLVM: @f.c = internal global [1 x i32] [i32 97], align 4
// OGCG: @f.c = internal global [1 x i32] [i32 97], align 4
