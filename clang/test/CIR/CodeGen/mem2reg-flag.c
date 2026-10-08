// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -O1 \
// RUN:   -disable-llvm-passes -emit-llvm %s -o - | FileCheck %s --check-prefix=ON
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -O1 \
// RUN:   -disable-llvm-passes -emit-llvm %s -o - -fno-clangir-mem2reg \
// RUN:   | FileCheck %s --check-prefix=OFF
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -O1 \
// RUN:   -disable-llvm-passes -emit-llvm %s -o - -fno-clangir-mem2reg \
// RUN:   -fclangir-mem2reg | FileCheck %s --check-prefix=ON
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -O1 \
// RUN:   -disable-llvm-passes -emit-llvm %s -o - -fclangir-mem2reg \
// RUN:   -fno-clangir-mem2reg | FileCheck %s --check-prefix=OFF
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -O0 \
// RUN:   -disable-llvm-passes -emit-llvm %s -o - | FileCheck %s --check-prefix=OFF

int add(int a, int b) {
  int c = a + b;
  return c;
}

// ON-LABEL: define {{.*}} @add(
// ON-NOT: alloca
// ON: ret i32

// OFF-LABEL: define {{.*}} @add(
// OFF: alloca
