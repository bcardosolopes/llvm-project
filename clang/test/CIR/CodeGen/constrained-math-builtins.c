// RUN: %clang_cc1 -triple x86_64-linux -ffp-exception-behavior=maytrap -w -emit-llvm %s -o - | FileCheck %s --check-prefixes=CHECK,OGCG --implicit-check-not='declare float @llvm.sin.f32(float)' --implicit-check-not='declare float @llvm.powi.f32.i32(float, i32)' --implicit-check-not='declare float @llvm.fma.f32(float, float, float)' --implicit-check-not='declare float @llvm.fmuladd.f32(float, float, float)' --implicit-check-not='declare double @llvm.exp10.f64(double)'
// RUN: %clang_cc1 -triple x86_64-linux -ffp-exception-behavior=maytrap -w -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefixes=CHECK,LLVM --implicit-check-not='declare float @llvm.sin.f32(float)' --implicit-check-not='declare float @llvm.powi.f32.i32(float, i32)' --implicit-check-not='declare float @llvm.fma.f32(float, float, float)' --implicit-check-not='declare float @llvm.fmuladd.f32(float, float, float)' --implicit-check-not='declare double @llvm.exp10.f64(double)'

#pragma float_control(except, on)

float unary(float x) {
  // CHECK-LABEL: define{{.*}} @unary(
  // CHECK: call float @llvm.experimental.constrained.sin.f32(float %{{.*}}, metadata !"round.tonearest", metadata !"fpexcept.strict") #[[STRICT_CALL_ATTR:[0-9]+]]
  return __builtin_sinf(x);
}
// OGCG: declare float @llvm.experimental.constrained.sin.f32(float, metadata, metadata) #[[DECL_ATTR:[0-9]+]]

float builtin_powi(float x, int y) {
  // CHECK-LABEL: define{{.*}} @builtin_powi(
  // CHECK: call float @llvm.experimental.constrained.powi.f32(float %{{.*}}, i32 %{{.*}}, metadata !"round.tonearest", metadata !"fpexcept.strict") #[[STRICT_CALL_ATTR]]
  return __builtin_powif(x, y);
}
// OGCG: declare float @llvm.experimental.constrained.powi.f32(float, i32, metadata, metadata) #[[DECL_ATTR]]

float builtin_fma(float a, float b, float c) {
  // CHECK-LABEL: define{{.*}} @builtin_fma(
  // CHECK: call float @llvm.experimental.constrained.fma.f32(float %{{.*}}, float %{{.*}}, float %{{.*}}, metadata !"round.tonearest", metadata !"fpexcept.strict") #[[STRICT_CALL_ATTR]]
  return __builtin_fmaf(a, b, c);
}
// OGCG: declare float @llvm.experimental.constrained.fma.f32(float, float, float, metadata, metadata) #[[DECL_ATTR]]

double builtin_exp10(double x) {
  // CHECK-LABEL: define{{.*}} @builtin_exp10(
  // CHECK: call double @exp10(double noundef %{{.*}}) #[[EXP10_CALL_ATTR:[0-9]+]]
  return __builtin_exp10(x);
}
// OGCG: declare double @exp10(double noundef) #[[LIBCALL_DECL_ATTR:[0-9]+]]
// LLVM: declare double @exp10(double noundef) #[[LIBCALL_DECL_ATTR:[0-9]+]]

#pragma STDC FP_CONTRACT ON
float contracted(float a, float b, float c) {
  // CHECK-LABEL: define{{.*}} @contracted(
  // CHECK: call float @llvm.experimental.constrained.fmuladd.f32(float %{{.*}}, float %{{.*}}, float %{{.*}}, metadata !"round.tonearest", metadata !"fpexcept.strict") #[[STRICT_CALL_ATTR]]
  return a * b + c;
}
// OGCG: declare float @llvm.experimental.constrained.fmuladd.f32(float, float, float, metadata, metadata) #[[DECL_ATTR]]
// LLVM: declare float @llvm.experimental.constrained.sin.f32(float, metadata, metadata) #[[DECL_ATTR:[0-9]+]]
// LLVM: declare float @llvm.experimental.constrained.powi.f32(float, i32, metadata, metadata) #[[DECL_ATTR]]
// LLVM: declare float @llvm.experimental.constrained.fma.f32(float, float, float, metadata, metadata) #[[DECL_ATTR]]
// LLVM: declare float @llvm.experimental.constrained.fmuladd.f32(float, float, float, metadata, metadata) #[[DECL_ATTR]]
// CHECK-DAG: attributes #[[DECL_ATTR]] = { nocallback nofree nosync nounwind strictfp willreturn memory(inaccessiblemem: readwrite) }
// CHECK-DAG: attributes #[[LIBCALL_DECL_ATTR]] = { nounwind{{.*}} }
// CHECK-DAG: attributes #[[STRICT_CALL_ATTR]] = { strictfp }
// CHECK-DAG: attributes #[[EXP10_CALL_ATTR]] = { nounwind strictfp }
