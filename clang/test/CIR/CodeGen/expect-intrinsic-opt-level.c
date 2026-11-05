// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -O0 -fclangir -disable-llvm-passes -emit-llvm %s -o - | FileCheck %s --check-prefix=O0
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -O1 -fclangir -disable-llvm-passes -emit-llvm %s -o - | FileCheck %s --check-prefix=O1
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -O2 -fclangir -disable-llvm-passes -emit-llvm %s -o - | FileCheck %s --check-prefix=O2
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -Os -fclangir -disable-llvm-passes -emit-llvm %s -o - | FileCheck %s --check-prefix=OS

int f(int x) { return __builtin_expect(x, 0); }

// O0-LABEL: define {{.*}} @f(
// O0-NOT: llvm.expect

// O1-LABEL: define {{.*}} @f(
// O1: call i64 @llvm.expect.i64

// O2-LABEL: define {{.*}} @f(
// O2: call i64 @llvm.expect.i64

// OS-LABEL: define {{.*}} @f(
// OS: call i64 @llvm.expect.i64
