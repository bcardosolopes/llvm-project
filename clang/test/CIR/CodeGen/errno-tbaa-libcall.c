// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -O1 -disable-llvm-passes \
// RUN:   -Wno-incompatible-library-redeclaration \
// RUN:   -ffast-math -fmath-errno -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -O1 -disable-llvm-passes \
// RUN:   -Wno-incompatible-library-redeclaration \
// RUN:   -ffast-math -fmath-errno -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Verify CIR tags eligible errno-touching FP libcalls with int TBAA metadata,
// matching OGCG, while leaving non-errno out-param libcalls untagged.

double frexp(double, int *exp);
void sincos(float a, float *s, float *c);

float f1(float x) { return __builtin_expf(x); }
double f2(double x, double y) { return __builtin_remainder(x, y); }
double f3(double x, int *exp) { return frexp(x, exp); }
void f4(float x, float *s, float *c) { sincos(x, s, c); }

// CIR-LABEL: define{{.*}} @f1(
// CIR: call{{.*}} @expf({{.*}}){{.*}}, !tbaa ![[ERRNO:[0-9]+]]
// CIR-LABEL: define{{.*}} @f2(
// CIR: call{{.*}} @remainder({{.*}}){{.*}}, !tbaa ![[ERRNO]]
// CIR-LABEL: define{{.*}} @f3(
// CIR: @frexp(double noundef{{.*}}, ptr noundef {{.*}}) #{{[0-9]+}}{{$}}
// CIR-LABEL: define{{.*}} @f4(
// CIR: @sincos(float noundef{{.*}}, ptr noundef {{.*}}, ptr noundef {{.*}}){{$}}
// CIR: !llvm.errno.tbaa = !{![[ERRNO]]}

// OGCG-LABEL: define{{.*}} @f1(
// OGCG: call{{.*}} @expf({{.*}}){{.*}}, !tbaa ![[ERRNO:[0-9]+]]
// OGCG-LABEL: define{{.*}} @f2(
// OGCG: call{{.*}} @remainder({{.*}}){{.*}}, !tbaa ![[ERRNO]]
// OGCG-LABEL: define{{.*}} @f3(
// OGCG: @frexp(double noundef{{.*}}, ptr noundef {{.*}}) #{{[0-9]+}}{{$}}
// OGCG-LABEL: define{{.*}} @f4(
// OGCG: @sincos(float noundef{{.*}}, ptr noundef {{.*}}, ptr noundef {{.*}}){{$}}
// OGCG: !llvm.errno.tbaa = !{![[ERRNO]]}
