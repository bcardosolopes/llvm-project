// RUN: %clang_cc1 -triple aarch64-pc-win32 -emit-llvm -fclangir %s -o - | FileCheck %s

struct HFA {
  float a, b, c;
};

void __attribute__((ms_abi)) hfa_vararg(struct HFA a, int b, ...);

void call_hfa_vararg(void) {
  hfa_vararg((struct HFA){1.0f, 2.0f, 3.0f}, 4,
             (struct HFA){5.0f, 6.0f, 7.0f});
}

// CHECK-LABEL: define dso_local void @call_hfa_vararg()
// CHECK: call void ([2 x i64], i32, ...) @hfa_vararg([2 x i64] {{.*}}, i32 noundef 4, [2 x i64] {{.*}})
// CHECK: declare dso_local void @hfa_vararg([2 x i64], i32 noundef, ...)
