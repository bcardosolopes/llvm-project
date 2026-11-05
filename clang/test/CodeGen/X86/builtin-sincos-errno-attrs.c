// RUN: %if cir-support %{ %clang_cc1 -triple x86_64-unknown-unknown -fclangir -emit-llvm \
// RUN:   -disable-llvm-passes -fmath-errno %s -o - | FileCheck %s %}
// RUN: %clang_cc1 -triple x86_64-unknown-unknown -emit-llvm \
// RUN:   -disable-llvm-passes -fmath-errno %s -o - | FileCheck %s

void test_sincos(double x, double *sp, double *cp) {
  __builtin_sincos(x, sp, cp);
}

void test_sincospi(double x, double *sp, double *cp) {
  __builtin_sincospi(x, sp, cp);
}

// CHECK-LABEL: define {{.*}} @test_sincos(
// CHECK: call void @sincos(double noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}}) #[[CALL_ATTR:[0-9]+]]
// CHECK: ret void
// CHECK: declare void @sincos(double noundef, ptr noundef, ptr noundef) #[[DECL_ATTR:[0-9]+]]

// CHECK-LABEL: define {{.*}} @test_sincospi(
// CHECK: call void @sincospi(double noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}}) #[[CALL_ATTR]]
// CHECK: ret void
// CHECK: declare void @sincospi(double noundef, ptr noundef, ptr noundef) #[[DECL_ATTR]]

// CHECK: attributes #[[DECL_ATTR]] = { nounwind{{.*}} }
// CHECK: attributes #[[CALL_ATTR]] = { nounwind }
