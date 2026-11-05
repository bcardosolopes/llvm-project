// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -O1 -ffp-exception-behavior=strict -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -O1 -ffp-exception-behavior=strict -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=CIR

int test_isinf_sign(double x) {
  // OGCG-LABEL: define{{.*}} @test_isinf_sign(
  // OGCG: %[[ABS_OGCG:.*]] = {{.*}}call double @llvm.fabs.f64(double %{{.*}}) #[[ABS_ATTR_OGCG:[0-9]+]]
  // OGCG: {{.*}}call i1 @llvm.experimental.constrained.fcmp.f64(double %[[ABS_OGCG]], double 0x7FF0000000000000, metadata !"oeq", metadata !"fpexcept.strict") #[[STRICT_ATTR_OGCG:[0-9]+]]
  // CIR-LABEL: define{{.*}} @test_isinf_sign(
  // CIR: %[[ABS_CIR:.*]] = {{.*}}call double @llvm.fabs.f64(double %{{.*}}) #[[ABS_ATTR_CIR:[0-9]+]]
  // CIR: {{.*}}call i1 @llvm.experimental.constrained.fcmp.f64(double %[[ABS_CIR]], double 0x7FF0000000000000, metadata !"oeq", metadata !"fpexcept.strict") #[[STRICT_ATTR_CIR:[0-9]+]]
  return __builtin_isinf_sign(x);
}

// OGCG: attributes #[[ABS_ATTR_OGCG]] = { strictfp memory(none) }
// OGCG: attributes #[[STRICT_ATTR_OGCG]] = { strictfp }
// CIR: attributes #[[ABS_ATTR_CIR]] = { strictfp memory(none) }
// CIR: attributes #[[STRICT_ATTR_CIR]] = { strictfp }
