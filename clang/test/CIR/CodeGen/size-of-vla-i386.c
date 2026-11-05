// RUN: %clang_cc1 -triple i386-unknown-linux-gnu -fclangir -emit-llvm %s -o - \
// RUN:   | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple i386-unknown-linux-gnu -emit-llvm %s -o - \
// RUN:   | FileCheck %s --check-prefix=OGCG

// The size of a variable-length array is computed in the target's size_t type,
// which is 32-bit on i386. Scaling the element count by the element size must
// build the element-size constant at the size_t width; building it at a fixed
// 64-bit width (as CIRGenModule::getSize returns) aborted CIR generation with
// "type and value bitwidth mismatch: 32 != 64".

unsigned long vla_size(int n) {
  int a[n];
  return sizeof(a);
}

// The element size (4) and the count are both i32; the scaling multiply is i32.
// LLVM-LABEL: @vla_size
// LLVM: mul {{.*}}i32 4,
// LLVM-NOT: i64

// OGCG-LABEL: @vla_size
// OGCG: mul {{.*}}i32 4,
