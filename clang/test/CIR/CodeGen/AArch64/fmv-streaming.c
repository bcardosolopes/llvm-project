// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +sme -fclangir -emit-llvm -o - %s | FileCheck %s

// Verify CIR matches OGCG for AArch64 FMV streaming functions:
// - TargetVersion uses ._M<feature> mangling (not generic .feature_feature)
// - Default versions get .default suffix
// - Callsite streaming attrs (aarch64_pstate_sm_enabled/compatible) are emitted
// - Resolvers use if-else branching (not select chains)

// CHECK-LABEL: define {{[^@]+}}@n_callee._Msve
// CHECK-SAME: () #[[locally_streaming_sve:[0-9]+]] {
//
// CHECK-LABEL: define {{[^@]+}}@n_callee._Msimd
// CHECK-SAME: () #[[locally_streaming_simd:[0-9]+]] {
//
__arm_locally_streaming __attribute__((target_clones("sve", "simd"))) void n_callee(void) {}
// CHECK-LABEL: define {{[^@]+}}@n_callee._Msme2
// CHECK-SAME: () #[[sme2:[0-9]+]] {
//
__attribute__((target_version("sme2"))) void n_callee(void) {}
// CHECK-LABEL: define {{[^@]+}}@n_callee.default
// CHECK-SAME: () #[[default:[0-9]+]] {
//
__attribute__((target_version("default"))) void n_callee(void) {}


// CHECK-LABEL: define {{[^@]+}}@s_callee._Msve
// CHECK-SAME: () #[[sve_streaming:[0-9]+]] {
//
// CHECK-LABEL: define {{[^@]+}}@s_callee._Msimd
// CHECK-SAME: () #[[simd_streaming:[0-9]+]] {
//
__attribute__((target_clones("sve", "simd"))) void s_callee(void) __arm_streaming {}
// CHECK-LABEL: define {{[^@]+}}@s_callee._Msme2
// CHECK-SAME: () #[[locally_streaming_sme2_streaming:[0-9]+]] {
//
__arm_locally_streaming __attribute__((target_version("sme2"))) void s_callee(void) __arm_streaming {}
// CHECK-LABEL: define {{[^@]+}}@s_callee.default
// CHECK-SAME: () #[[default_streaming:[0-9]+]] {
//
__attribute__((target_version("default"))) void s_callee(void) __arm_streaming {}


// CHECK-LABEL: define {{[^@]+}}@sc_callee._Msve
// CHECK-SAME: () #[[sve_streaming_compatible:[0-9]+]] {
//
// CHECK-LABEL: define {{[^@]+}}@sc_callee._Msimd
// CHECK-SAME: () #[[simd_streaming_compatible:[0-9]+]] {
//
__attribute__((target_clones("sve", "simd"))) void sc_callee(void) __arm_streaming_compatible {}
// CHECK-LABEL: define {{[^@]+}}@sc_callee._Msme2
// CHECK-SAME: () #[[locally_streaming_sme2_streaming_compatible:[0-9]+]] {
//
__arm_locally_streaming __attribute__((target_version("sme2"))) void sc_callee(void) __arm_streaming_compatible {}
// CHECK-LABEL: define {{[^@]+}}@sc_callee.default
// CHECK-SAME: () #[[default_streaming_compatible:[0-9]+]] {
//
__attribute__((target_version("default"))) void sc_callee(void) __arm_streaming_compatible {}


// CHECK-LABEL: define {{[^@]+}}@n_caller
// CHECK-SAME: () #[[caller:[0-9]+]] {
// CHECK:    call void @n_callee()
// CHECK:    call void @s_callee() #[[callsite_streaming:[0-9]+]]
// CHECK:    call void @sc_callee() #[[callsite_streaming_compatible:[0-9]+]]
//
void n_caller(void) {
  n_callee();
  s_callee();
  sc_callee();
}


// CHECK-LABEL: define {{[^@]+}}@s_caller
// CHECK-SAME: () #[[caller_streaming:[0-9]+]] {
// CHECK:    call void @n_callee()
// CHECK:    call void @s_callee() #[[callsite_streaming]]
// CHECK:    call void @sc_callee() #[[callsite_streaming_compatible]]
//
void s_caller(void) __arm_streaming {
  n_callee();
  s_callee();
  sc_callee();
}


// CHECK-LABEL: define {{[^@]+}}@sc_caller
// CHECK-SAME: () #[[caller_streaming_compatible:[0-9]+]] {
// CHECK:    call void @n_callee()
// CHECK:    call void @s_callee() #[[callsite_streaming]]
// CHECK:    call void @sc_callee() #[[callsite_streaming_compatible]]
//
void sc_caller(void) __arm_streaming_compatible {
  n_callee();
  s_callee();
  sc_callee();
}


// Verify resolver uses if-else branching structure (not select).
// CHECK-LABEL: define {{[^@]+}}@n_callee.resolver
// CHECK:    call void @__init_cpu_features_resolver()
// CHECK:    load i64, ptr @__aarch64_cpu_features
// CHECK:    br i1 %{{.*}}, label %{{.*}}, label %{{.*}}
// CHECK:    ret ptr @n_callee._Msme2
// CHECK:    ret ptr @n_callee._Msve
// CHECK:    ret ptr @n_callee._Msimd
// CHECK:    ret ptr @n_callee.default

// CHECK: attributes #[[locally_streaming_sve]] = {{.*}} "aarch64_pstate_sm_body"
// CHECK: attributes #[[locally_streaming_simd]] = {{.*}} "aarch64_pstate_sm_body"
// CHECK: attributes #[[sme2]] = {{.*}}
// CHECK: attributes #[[default]] = {{.*}}
// CHECK: attributes #[[sve_streaming]] = {{.*}} "aarch64_pstate_sm_enabled"
// CHECK: attributes #[[simd_streaming]] = {{.*}} "aarch64_pstate_sm_enabled"
// CHECK: attributes #[[locally_streaming_sme2_streaming]] = {{.*}} "aarch64_pstate_sm_body" "aarch64_pstate_sm_enabled"
// CHECK: attributes #[[default_streaming]] = {{.*}} "aarch64_pstate_sm_enabled"
// CHECK: attributes #[[sve_streaming_compatible]] = {{.*}} "aarch64_pstate_sm_compatible"
// CHECK: attributes #[[simd_streaming_compatible]] = {{.*}} "aarch64_pstate_sm_compatible"
// CHECK: attributes #[[locally_streaming_sme2_streaming_compatible]] = {{.*}} "aarch64_pstate_sm_body" "aarch64_pstate_sm_compatible"
// CHECK: attributes #[[default_streaming_compatible]] = {{.*}} "aarch64_pstate_sm_compatible"
// CHECK: attributes #[[caller]] = {{.*}}
// CHECK: attributes #[[caller_streaming]] = {{.*}} "aarch64_pstate_sm_enabled"
// CHECK: attributes #[[caller_streaming_compatible]] = {{.*}} "aarch64_pstate_sm_compatible"
// CHECK: attributes #[[callsite_streaming]] = {{.*}} "aarch64_pstate_sm_enabled"
// CHECK: attributes #[[callsite_streaming_compatible]] = {{.*}} "aarch64_pstate_sm_compatible"
