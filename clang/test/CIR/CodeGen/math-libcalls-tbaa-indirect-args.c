// RUN: %clang_cc1 %s -O3 -fmath-errno -disable-llvm-passes -triple x86_64-unknown-unknown -emit-llvm -o - | FileCheck %s
// RUN: %clang_cc1 %s -O3 -fmath-errno -disable-llvm-passes -triple x86_64-unknown-unknown -fclangir -emit-llvm -o - | FileCheck %s

long double test_powl(long double a, long double b) {
  return __builtin_powl(a, b);
}

// CHECK-LABEL: define dso_local x86_fp80 @test_powl(
// CHECK: [[POWL:%.*]] = {{(tail )?}}call x86_fp80 @powl(x86_fp80 noundef {{.*}}, x86_fp80 noundef {{.*}}){{.*}}!tbaa ![[ERRNO:[0-9]+]]
// CHECK: declare x86_fp80 @powl(x86_fp80 noundef, x86_fp80 noundef){{.*}}

_Complex long double test_cargl(_Complex long double cld) {
  long double angle = __builtin_cargl(cld);
  return angle * cld;
}

// CHECK-LABEL: define dso_local { x86_fp80, x86_fp80 } @test_cargl(
// CHECK-NOT: @cargl{{.*}}!tbaa
// CHECK: [[CARGL:%.*]] = {{(tail )?}}call x86_fp80 @cargl(ptr noundef byval({ x86_fp80, x86_fp80 }) align 16 {{.*}})
// CHECK: declare x86_fp80 @cargl(ptr noundef byval({ x86_fp80, x86_fp80 }) align 16){{.*}}

// CHECK: !llvm.errno.tbaa = !{![[ERRNO]]}
