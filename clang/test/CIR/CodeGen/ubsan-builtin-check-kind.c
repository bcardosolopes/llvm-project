// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=builtin -fclangir \
// RUN:   -emit-llvm -O1 %s -o - | FileCheck %s

// Verify CIR emits correct kind byte in invalid_builtin_data:
//   0 = CTZ, 1 = CLZ, 2 = assume

void test_ctz(unsigned n) {
  __builtin_ctz(n);
}

void test_clz(unsigned n) {
  __builtin_clz(n);
}

void test_assume(int x) {
  __builtin_assume(x > 0);
}

// CHECK-DAG: private unnamed_addr global { { ptr, i32, i32 }, i8 } { { ptr, i32, i32 } { ptr {{@.src[^,]*}}, i32 {{[0-9]+}}, i32 {{[0-9]+}} }, i8 0 }
// CHECK-DAG: private unnamed_addr global { { ptr, i32, i32 }, i8 } { { ptr, i32, i32 } { ptr {{@.src[^,]*}}, i32 {{[0-9]+}}, i32 {{[0-9]+}} }, i8 1 }
// CHECK-DAG: private unnamed_addr global { { ptr, i32, i32 }, i8 } { { ptr, i32, i32 } { ptr {{@.src[^,]*}}, i32 {{[0-9]+}}, i32 {{[0-9]+}} }, i8 2 }
