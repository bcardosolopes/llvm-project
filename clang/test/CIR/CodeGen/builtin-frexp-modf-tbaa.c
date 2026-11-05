// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -O1 -disable-llvm-passes %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -O1 -disable-llvm-passes %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=OGCG

float test_modff_tbaa(float val, float *iptr) {
  return __builtin_modff(val, iptr);
}

double test_modf_tbaa(double val, double *iptr) {
  return __builtin_modf(val, iptr);
}

long double test_modfl_tbaa(long double val, long double *iptr) {
  return __builtin_modfl(val, iptr);
}

float test_frexpf_tbaa(float val, int *exp) {
  return __builtin_frexpf(val, exp);
}

double test_frexp_tbaa(double val, int *exp) {
  return __builtin_frexp(val, exp);
}

long double test_frexpl_tbaa(long double val, int *exp) {
  return __builtin_frexpl(val, exp);
}

// CIR-LABEL: @test_modff_tbaa
// CIR: call { float, float } @llvm.modf.f32(float %{{.*}})
// CIR: store float %{{.*}}, ptr %{{.*}}, align 4, !tbaa ![[CIR_FLOAT_TAG:[0-9]+]]

// OGCG-LABEL: @test_modff_tbaa
// OGCG: call { float, float } @llvm.modf.f32(float %{{.*}})
// OGCG: store float %{{.*}}, ptr %{{.*}}, align 4, !tbaa ![[OG_FLOAT_TAG:[0-9]+]]

// CIR-LABEL: @test_modf_tbaa
// CIR: call { double, double } @llvm.modf.f64(double %{{.*}})
// CIR: store double %{{.*}}, ptr %{{.*}}, align 8, !tbaa ![[CIR_DOUBLE_TAG:[0-9]+]]

// OGCG-LABEL: @test_modf_tbaa
// OGCG: call { double, double } @llvm.modf.f64(double %{{.*}})
// OGCG: store double %{{.*}}, ptr %{{.*}}, align 8, !tbaa ![[OG_DOUBLE_TAG:[0-9]+]]

// CIR-LABEL: @test_modfl_tbaa
// CIR: call { x86_fp80, x86_fp80 } @llvm.modf.f80(x86_fp80 %{{.*}})
// CIR: store x86_fp80 %{{.*}}, ptr %{{.*}}, align 16, !tbaa ![[CIR_LONG_DOUBLE_TAG:[0-9]+]]

// OGCG-LABEL: @test_modfl_tbaa
// OGCG: call { x86_fp80, x86_fp80 } @llvm.modf.f80(x86_fp80 %{{.*}})
// OGCG: store x86_fp80 %{{.*}}, ptr %{{.*}}, align 16, !tbaa ![[OG_LONG_DOUBLE_TAG:[0-9]+]]

// CIR-LABEL: @test_frexpf_tbaa
// CIR: call { float, i32 } @llvm.frexp.f32.i32(float %{{.*}})
// CIR: store i32 %{{.*}}, ptr %{{.*}}, align 4, !tbaa ![[CIR_INT_TAG:[0-9]+]]

// OGCG-LABEL: @test_frexpf_tbaa
// OGCG: call { float, i32 } @llvm.frexp.f32.i32(float %{{.*}})
// OGCG: store i32 %{{.*}}, ptr %{{.*}}, align 4, !tbaa ![[OG_INT_TAG:[0-9]+]]

// CIR-LABEL: @test_frexp_tbaa
// CIR: call { double, i32 } @llvm.frexp.f64.i32(double %{{.*}})
// CIR: store i32 %{{.*}}, ptr %{{.*}}, align 4, !tbaa ![[CIR_INT_TAG]]

// OGCG-LABEL: @test_frexp_tbaa
// OGCG: call { double, i32 } @llvm.frexp.f64.i32(double %{{.*}})
// OGCG: store i32 %{{.*}}, ptr %{{.*}}, align 4, !tbaa ![[OG_INT_TAG]]

// CIR-LABEL: @test_frexpl_tbaa
// CIR: call { x86_fp80, i32 } @llvm.frexp.f80.i32(x86_fp80 %{{.*}})
// CIR: store i32 %{{.*}}, ptr %{{.*}}, align 4, !tbaa ![[CIR_INT_TAG]]

// OGCG-LABEL: @test_frexpl_tbaa
// OGCG: call { x86_fp80, i32 } @llvm.frexp.f80.i32(x86_fp80 %{{.*}})
// OGCG: store i32 %{{.*}}, ptr %{{.*}}, align 4, !tbaa ![[OG_INT_TAG]]
//
// CIR-DAG: ![[CIR_ROOT:[0-9]+]] = !{!"Simple C/C++ TBAA"}
// CIR-DAG: ![[CIR_CHAR:[0-9]+]] = !{!"omnipotent char", ![[CIR_ROOT]], i64 0}
// CIR-DAG: ![[CIR_FLOAT_NODE:[0-9]+]] = !{!"float", ![[CIR_CHAR]], i64 0}
// CIR-DAG: ![[CIR_DOUBLE_NODE:[0-9]+]] = !{!"double", ![[CIR_CHAR]], i64 0}
// CIR-DAG: ![[CIR_LONG_DOUBLE_NODE:[0-9]+]] = !{!"long double", ![[CIR_CHAR]], i64 0}
// CIR-DAG: ![[CIR_INT_NODE:[0-9]+]] = !{!"int", ![[CIR_CHAR]], i64 0}
// CIR-DAG: ![[CIR_FLOAT_TAG]] = !{![[CIR_FLOAT_NODE]], ![[CIR_FLOAT_NODE]], i64 0}
// CIR-DAG: ![[CIR_DOUBLE_TAG]] = !{![[CIR_DOUBLE_NODE]], ![[CIR_DOUBLE_NODE]], i64 0}
// CIR-DAG: ![[CIR_LONG_DOUBLE_TAG]] = !{![[CIR_LONG_DOUBLE_NODE]], ![[CIR_LONG_DOUBLE_NODE]], i64 0}
// CIR-DAG: ![[CIR_INT_TAG]] = !{![[CIR_INT_NODE]], ![[CIR_INT_NODE]], i64 0}
//
// OGCG-DAG: ![[OG_ROOT:[0-9]+]] = !{!"Simple C/C++ TBAA"}
// OGCG-DAG: ![[OG_CHAR:[0-9]+]] = !{!"omnipotent char", ![[OG_ROOT]], i64 0}
// OGCG-DAG: ![[OG_FLOAT_NODE:[0-9]+]] = !{!"float", ![[OG_CHAR]], i64 0}
// OGCG-DAG: ![[OG_DOUBLE_NODE:[0-9]+]] = !{!"double", ![[OG_CHAR]], i64 0}
// OGCG-DAG: ![[OG_LONG_DOUBLE_NODE:[0-9]+]] = !{!"long double", ![[OG_CHAR]], i64 0}
// OGCG-DAG: ![[OG_INT_NODE:[0-9]+]] = !{!"int", ![[OG_CHAR]], i64 0}
// OGCG-DAG: ![[OG_FLOAT_TAG]] = !{![[OG_FLOAT_NODE]], ![[OG_FLOAT_NODE]], i64 0}
// OGCG-DAG: ![[OG_DOUBLE_TAG]] = !{![[OG_DOUBLE_NODE]], ![[OG_DOUBLE_NODE]], i64 0}
// OGCG-DAG: ![[OG_LONG_DOUBLE_TAG]] = !{![[OG_LONG_DOUBLE_NODE]], ![[OG_LONG_DOUBLE_NODE]], i64 0}
// OGCG-DAG: ![[OG_INT_TAG]] = !{![[OG_INT_NODE]], ![[OG_INT_NODE]], i64 0}
