// RUN: %clang_cc1 -triple aarch64 -target-feature -fp-armv8 -target-abi aapcs-soft -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefixes=CHECK,SOFT
// RUN: %clang_cc1 -triple aarch64 -target-feature -fp-armv8 -target-abi aapcs-soft -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefixes=CHECK,SOFT-DIRECT

// The va_list layout stays the same as hard-float AAPCS64.
// CHECK: %struct.__va_list = type { ptr, ptr, ptr, i32, i32 }

// Scalar floating-point types keep their IR type; the backend handles the
// soft-float register assignment.
// SOFT-LABEL: define dso_local half @test0(half{{( noundef)?}} %{{.*}})
// SOFT-LABEL: define dso_local bfloat @test1(bfloat{{( noundef)?}} %{{.*}})
// SOFT-LABEL: define dso_local float @test2(float{{( noundef)?}} %{{.*}})
// SOFT-LABEL: define dso_local double @test3(double{{( noundef)?}} %{{.*}})
// SOFT-LABEL: define dso_local fp128 @test4(fp128{{( noundef)?}} %{{.*}})
__fp16 test0(__fp16 a) { return a; }
__bf16 test1(__bf16 a) { return a; }
float test2(float a) { return a; }
double test3(double a) { return a; }
long double test4(long double a) { return a; }

// The soft-float PCS does not classify any aggregates as HFAs/HVAs, so these
// must be coerced like integer aggregates instead of using FP register rules.
struct A {
  float x;
};
// SOFT-LABEL: define dso_local i32 @test10(i64 %{{.*}})
// SOFT-DIRECT-LABEL: define dso_local i32 @test10(i64 %{{.*}})
struct A test10(struct A a) { return a; }

struct B {
  double x;
  double y;
};
// SOFT-LABEL: define dso_local [2 x i64] @test11([2 x i64] %{{.*}})
// SOFT-DIRECT-LABEL: define dso_local [2 x i64] @test11([2 x i64] %{{.*}})
struct B test11(struct B a) { return a; }

// SOFT-LABEL: define dso_local i32 @useA(i64 %{{.*}})
// SOFT-NOT: load float
// SOFT: call i32 @test10(i64 %{{.*}})
// SOFT-DIRECT-LABEL: define dso_local i32 @useA(i64 %{{.*}})
// SOFT-DIRECT-NOT: load float
// SOFT-DIRECT: call i32 @test10(i64 %{{.*}})
struct A useA(struct A a) { return test10(a); }

// SOFT-LABEL: define dso_local [2 x i64] @useB([2 x i64] %{{.*}})
// SOFT: call [2 x i64] @test11([2 x i64] %{{.*}})
// SOFT-DIRECT-LABEL: define dso_local [2 x i64] @useB([2 x i64] %{{.*}})
// SOFT-DIRECT: call [2 x i64] @test11([2 x i64] %{{.*}})
struct B useB(struct B b) { return test11(b); }

#include <stdarg.h>

// Soft-float AAPCS64 still uses the standard AArch64 va_list layout, but
// floating-point varargs are fetched from the GPR save area instead of the
// FP/SIMD save area.
// SOFT-LABEL: define dso_local double @test20(i32{{( noundef)?}} %{{.*}}, ...)
// SOFT: {{%.*}} = getelementptr inbounds nuw %struct.__va_list, ptr %{{.*}}, i32 0, i32 3
// SOFT: {{%.*}} = getelementptr inbounds nuw %struct.__va_list, ptr %{{.*}}, i32 0, i32 1
// SOFT-DIRECT-LABEL: define dso_local double @test20(i32{{( noundef)?}} %{{.*}}, ...)
// SOFT-DIRECT: {{%.*}} = getelementptr inbounds nuw %struct.__va_list, ptr %{{.*}}, i32 0, i32 3
// SOFT-DIRECT: {{%.*}} = getelementptr inbounds nuw %struct.__va_list, ptr %{{.*}}, i32 0, i32 1
double test20(int a, ...) {
  va_list vl;
  va_start(vl, a);
  return va_arg(vl, double);
}
