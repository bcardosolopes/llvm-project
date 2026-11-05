// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -O0 %s -o - | FileCheck %s

// Verify direct-CIR target_clones resolvers declare and use __cpu_features2 for
// x86-64 ISA-level dispatch.

__attribute__((target_clones("default", "arch=x86-64", "arch=x86-64-v2",
                              "arch=x86-64-v3", "arch=x86-64-v4")))
int isa_level(int x) {
  return x;
}

int call_isa_level(int x) { return isa_level(x); }

// CHECK-DAG: @__cpu_model = external dso_local global { i32, i32, i32, [1 x i32] }
// CHECK-DAG: @__cpu_features2 = external dso_local global [3 x i32]
// CHECK: define weak_odr ptr @isa_level.resolver()
// CHECK: call void @__cpu_indicator_init()
// CHECK: and i32 %{{.*}}, 4
// CHECK: ret ptr @isa_level.arch_x86-64-v4.3
// CHECK: and i32 %{{.*}}, 2
// CHECK: ret ptr @isa_level.arch_x86-64-v3.2
// CHECK: and i32 %{{.*}}, 1
// CHECK: ret ptr @isa_level.arch_x86-64-v2.1
// CHECK: and i32 %{{.*}}, -2147483648
// CHECK: ret ptr @isa_level.arch_x86-64.0
// CHECK: ret ptr @isa_level.default.4
// CHECK-LABEL: define dso_local i32 @call_isa_level(
// CHECK: call i32 @isa_level(
