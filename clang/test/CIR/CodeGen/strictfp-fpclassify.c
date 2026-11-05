// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -ffp-exception-behavior=strict -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -ffp-exception-behavior=strict -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=CIR

int test_fpclassify(double x) {
  // OGCG-LABEL: define{{.*}} @test_fpclassify(
  // OGCG: %[[ISZERO:.*]] = call i1 @llvm.experimental.constrained.fcmp.f64(double %{{.*}}, double 0.000000e+00, metadata !"oeq", metadata !"fpexcept.strict") #[[STRICT:[0-9]+]]
  // OGCG-NEXT: br i1 %[[ISZERO]], label %[[END:[^,]+]], label %[[NOTZERO:[^ ]+]]
  // OGCG: [[NOTZERO]]:
  // OGCG: %[[ISNAN:.*]] = call i1 @llvm.experimental.constrained.fcmp.f64(double %{{.*}}, double %{{.*}}, metadata !"uno", metadata !"fpexcept.strict") #[[STRICT]]
  // OGCG-NEXT: br i1 %[[ISNAN]], label %[[END]], label %[[NOTNAN:[^ ]+]]
  // OGCG: [[NOTNAN]]:
  // OGCG: %[[ABS:.*]] = call double @llvm.fabs.f64(double %{{.*}}) #[[ABSATTR_OGCG:[0-9]+]]
  // OGCG: %[[ISINF:.*]] = call i1 @llvm.experimental.constrained.fcmp.f64(double %[[ABS]], double 0x7FF0000000000000, metadata !"oeq", metadata !"fpexcept.strict") #[[STRICT]]
  // OGCG-NEXT: br i1 %[[ISINF]], label %[[END]], label %[[NOTINF:[^ ]+]]
  // OGCG: [[NOTINF]]:
  // OGCG: %[[ISNORMAL:.*]] = call i1 @llvm.experimental.constrained.fcmp.f64(double %[[ABS]], double 0x10000000000000, metadata !"uge", metadata !"fpexcept.strict") #[[STRICT]]
  // OGCG: %[[NORMALORSUB:.*]] = select i1 %[[ISNORMAL]], i32 2, i32 3
  // CIR-LABEL: define{{.*}} @test_fpclassify(
  // CIR: %[[ISZERO_CIR:.*]] = call i1 @llvm.experimental.constrained.fcmp.f64(double %{{.*}}, double 0.000000e+00, metadata !"oeq", metadata !"fpexcept.strict") #[[STRICT_CIR:[0-9]+]]
  // CIR-NEXT: br i1 %[[ISZERO_CIR]], label %[[ZERO:[0-9]+]], label %[[NOTZERO_CIR:[0-9]+]]
  // CIR: [[NOTZERO_CIR]]:
  // CIR: %[[ISNAN_CIR:.*]] = call i1 @llvm.experimental.constrained.fcmp.f64(double %{{.*}}, double %{{.*}}, metadata !"uno", metadata !"fpexcept.strict") #[[STRICT_CIR]]
  // CIR-NEXT: br i1 %[[ISNAN_CIR]], label %[[NAN:[0-9]+]], label %[[NOTNAN_CIR:[0-9]+]]
  // CIR: [[NOTNAN_CIR]]:
  // CIR: %[[ABS_CIR:.*]] = call double @llvm.fabs.f64(double %{{.*}}) #[[ABSATTR_CIR:[0-9]+]]
  // CIR: %[[ISINF_CIR:.*]] = call i1 @llvm.experimental.constrained.fcmp.f64(double %[[ABS_CIR]], double 0x7FF0000000000000, metadata !"oeq", metadata !"fpexcept.strict") #[[STRICT_CIR]]
  // CIR-NEXT: br i1 %[[ISINF_CIR]], label %[[INF:[0-9]+]], label %[[NOTINF_CIR:[0-9]+]]
  // CIR: [[NOTINF_CIR]]:
  // CIR: %[[ISNORMAL_CIR:.*]] = call i1 @llvm.experimental.constrained.fcmp.f64(double %[[ABS_CIR]], double 0x10000000000000, metadata !"uge", metadata !"fpexcept.strict") #[[STRICT_CIR]]
  // CIR-NEXT: br i1 %[[ISNORMAL_CIR]], label %[[NORMAL:[0-9]+]], label %[[SUB:[0-9]+]]
  // CIR: [[NORMAL]]:
  // CIR-NEXT: br label %[[NORMALORSUB_JOIN:[0-9]+]]
  // CIR: [[SUB]]:
  // CIR-NEXT: br label %[[NORMALORSUB_JOIN]]
  // CIR: [[NORMALORSUB_JOIN]]:
  // CIR: %[[NORMALORSUB_CIR:.*]] = phi i32 [ 3, %[[SUB]] ], [ 2, %[[NORMAL]] ]
  return __builtin_fpclassify(0, 1, 2, 3, 4, x);
}

// OGCG: attributes #[[STRICT]] = { strictfp }
// CIR: attributes #[[STRICT_CIR]] = { strictfp }
// OGCG: attributes #[[ABSATTR_OGCG]] = { strictfp memory(none) }
// CIR: attributes #[[ABSATTR_CIR]] = { strictfp memory(none) }
