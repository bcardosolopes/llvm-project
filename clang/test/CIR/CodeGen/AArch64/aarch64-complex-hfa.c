// RUN: %clang_cc1 -triple aarch64-linux-gnu -fclangir -emit-llvm -o - %s | FileCheck %s

// Test that complex types are recognized as Homogeneous Floating-point
// Aggregates (HFAs) on AArch64 and passed/returned using float registers
// instead of integer coercion.

_Complex float gcf;
_Complex double gcd;

// _Complex float: HFA with base=float, members=2 → [2 x float]
// CHECK: define dso_local void @test_cf([2 x float] noundef alignstack(8) %{{.*}})
void test_cf(_Complex float c) { gcf = c; }

// _Complex double: HFA with base=double, members=2 → [2 x double]
// CHECK: define dso_local void @test_cd([2 x double] noundef alignstack(8) %{{.*}})
void test_cd(_Complex double c) { gcd = c; }

// Return _Complex float: { float, float }
// CHECK: define dso_local { float, float } @ret_cf()
_Complex float ret_cf(void) { return gcf; }

// Return _Complex double: { double, double }
// CHECK: define dso_local { double, double } @ret_cd()
_Complex double ret_cd(void) { return gcd; }

// Struct containing complex: the complex member is an HFA leaf
struct HasComplex { _Complex float cf; };
void use_hc(struct HasComplex *);

// CHECK: define dso_local void @test_has_complex([2 x float] alignstack(8) %{{.*}})
void test_has_complex(struct HasComplex s) { use_hc(&s); }
