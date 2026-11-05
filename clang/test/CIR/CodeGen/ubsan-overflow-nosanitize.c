// RUN: %clang_cc1 -triple x86_64-linux-gnu -fclangir -emit-llvm -O1 \
// RUN:   -fsanitize=unsigned-integer-overflow \
// RUN:   -fsanitize-recover=unsigned-integer-overflow \
// RUN:   -o - %s | FileCheck %s

// Verify that user-written source loads do NOT get !nosanitize metadata,
// while the sanitizer check instructions DO get it.

extern volatile unsigned x, y, z;

void test(void) {
  x = y + z;
}

// Source loads should NOT have !nosanitize
// CHECK: load volatile i32, ptr @y,
// CHECK-SAME: !tbaa
// CHECK-NOT: !nosanitize
// CHECK: load volatile i32, ptr @z,
// CHECK-SAME: !tbaa
// CHECK-NOT: !nosanitize

// The overflow check and handler SHOULD have !nosanitize
// CHECK: uadd.with.overflow.i32
// CHECK-SAME: !nosanitize
