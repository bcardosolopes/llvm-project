// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -o %t.cir.ll %s
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.cir.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -o %t.ogcg.ll %s
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

typedef double * __attribute__((align_value(64))) aligned_double;

// LLVM-LABEL: @_Z3fooPdS_Rd(
// LLVM-SAME: ptr noundef align 64
// LLVM-SAME: ptr noundef align 32
// LLVM-SAME: ptr noundef nonnull align 128 dereferenceable(8)
// OGCG-LABEL: @_Z3fooPdS_Rd(
// OGCG-SAME: ptr noundef align 64
// OGCG-SAME: ptr noundef align 32
// OGCG-SAME: ptr noundef nonnull align 128 dereferenceable(8)
void foo(aligned_double x, double *y __attribute__((align_value(32))),
         double &z __attribute__((align_value(128)))) {}

struct ad_struct {
  aligned_double a;
};

// LLVM-LABEL: @_Z3fooR9ad_struct(
// LLVM: call void @llvm.assume(i1 true) [ "align"(ptr %{{.*}}, i64 64) ]
// OGCG-LABEL: @_Z3fooR9ad_struct(
// OGCG: call void @llvm.assume(i1 true) [ "align"(ptr %{{.*}}, i64 64) ]
double *foo(ad_struct &x) { return x.a; }

// LLVM-LABEL: @_Z3barPPd(
// LLVM: call void @llvm.assume(i1 true) [ "align"(ptr %{{.*}}, i64 64) ]
// OGCG-LABEL: @_Z3barPPd(
// OGCG: call void @llvm.assume(i1 true) [ "align"(ptr %{{.*}}, i64 64) ]
double *bar(aligned_double *x) { return *x; }

// LLVM-LABEL: @_Z3no1PPd(
// LLVM-NOT: call void @llvm.assume
// LLVM: ret ptr %0
// OGCG-LABEL: @_Z3no1PPd(
// OGCG: %{{.*}} = load ptr, ptr %{{.*}}, align 8
// OGCG-NOT: call void @llvm.assume
// OGCG: ret ptr %{{.*}}
double **no1(aligned_double *x) { return x; }
