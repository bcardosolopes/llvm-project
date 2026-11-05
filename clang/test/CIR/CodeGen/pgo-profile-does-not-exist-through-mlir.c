// RUN: not %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir \
// RUN:   -fno-clangir-direct-lowering -emit-llvm %s -o - \
// RUN:   -fprofile-instrument-use=clang \
// RUN:   -fprofile-instrument-use-path=%t.nonexistent.profdata 2>&1 | FileCheck %s

// CHECK: error: error in reading profile {{.*}}.nonexistent.profdata:
// CHECK-NOT: CIR-to-CIR transformation failed
// CHECK-NOT: Assertion failed

int main() { return 0; }
