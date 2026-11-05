// REQUIRES: amdgpu-registered-target
// RUN: %clang_cc1 -triple amdgcn-unknown-unknown -cl-std=CL2.0 -fclangir \
// RUN:            -target-cpu tahiti -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple amdgcn-unknown-unknown -cl-std=CL2.0 -fclangir \
// RUN:            -target-cpu tahiti -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s
// RUN: %clang_cc1 -triple amdgcn-unknown-unknown -cl-std=CL2.0 \
// RUN:            -target-cpu tahiti -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ll %s

// Regression coverage for the amdgcn CIRGen builtins that map 1:1 to
// llvm.amdgcn.* intrinsics: wave reductions (including the u->umin/umax and
// b->bitwise intrinsic-name remaps), lane broadcasts, the icmp/fcmp lane-mask
// builtins (fixed i64 result overload + predicate immarg), the transcendental
// approximations, and dispatch.ptr (call-site align/dereferenceable ret attrs).

#pragma OPENCL EXTENSION cl_khr_fp64 : enable
typedef unsigned int uint;
typedef unsigned long ulong;

// CIR-LABEL: @test_wave_reduce_add_u32
// CIR: cir.call_llvm_intrinsic "amdgcn.wave.reduce.add" {{.*}} : (!u32i, !s32i) -> !u32i
// LLVM: call i32 @llvm.amdgcn.wave.reduce.add.i32(i32 %{{.*}}, i32 0)
// OGCG: call i32 @llvm.amdgcn.wave.reduce.add.i32(i32 %{{.*}}, i32 0)
void test_wave_reduce_add_u32(global uint *out, uint in) {
  *out = __builtin_amdgcn_wave_reduce_add_u32(in, 0);
}

// CIR-LABEL: @test_wave_reduce_min_u32
// CIR: cir.call_llvm_intrinsic "amdgcn.wave.reduce.umin" {{.*}} : (!u32i, !s32i) -> !u32i
// LLVM: call i32 @llvm.amdgcn.wave.reduce.umin.i32(i32 %{{.*}}, i32 0)
// OGCG: call i32 @llvm.amdgcn.wave.reduce.umin.i32(i32 %{{.*}}, i32 0)
void test_wave_reduce_min_u32(global uint *out, uint in) {
  *out = __builtin_amdgcn_wave_reduce_min_u32(in, 0);
}

// CIR-LABEL: @test_wave_reduce_and_b32
// CIR: cir.call_llvm_intrinsic "amdgcn.wave.reduce.and" {{.*}} : (!s32i, !s32i) -> !s32i
// LLVM: call i32 @llvm.amdgcn.wave.reduce.and.i32(i32 %{{.*}}, i32 0)
// OGCG: call i32 @llvm.amdgcn.wave.reduce.and.i32(i32 %{{.*}}, i32 0)
void test_wave_reduce_and_b32(global int *out, int in) {
  *out = __builtin_amdgcn_wave_reduce_and_b32(in, 0);
}

// CIR-LABEL: @test_readlane
// CIR: cir.call_llvm_intrinsic "amdgcn.readlane" {{.*}} : (!s32i, !s32i) -> !s32i
// LLVM: call i32 @llvm.amdgcn.readlane.i32(i32 %{{.*}}, i32 %{{.*}})
// OGCG: call i32 @llvm.amdgcn.readlane.i32(i32 %{{.*}}, i32 %{{.*}})
void test_readlane(global int *out, int a, int b) {
  *out = __builtin_amdgcn_readlane(a, b);
}

// CIR-LABEL: @test_readfirstlane
// CIR: cir.call_llvm_intrinsic "amdgcn.readfirstlane" {{.*}} : (!s32i) -> !s32i
// LLVM: call i32 @llvm.amdgcn.readfirstlane.i32(i32 %{{.*}})
// OGCG: call i32 @llvm.amdgcn.readfirstlane.i32(i32 %{{.*}})
void test_readfirstlane(global int *out, int a) {
  *out = __builtin_amdgcn_readfirstlane(a);
}

// CIR-LABEL: @test_sicmp
// CIR: cir.call_llvm_intrinsic "amdgcn.icmp" {{.*}} : (!s32i, !s32i, !s32i) -> !u64i
// LLVM: call i64 @llvm.amdgcn.icmp.i64.i32(i32 %{{.*}}, i32 %{{.*}}, i32 32)
// OGCG: call i64 @llvm.amdgcn.icmp.i64.i32(i32 %{{.*}}, i32 %{{.*}}, i32 32)
void test_sicmp(global ulong *out, int a, int b) {
  *out = __builtin_amdgcn_sicmp(a, b, 32);
}

// CIR-LABEL: @test_fcmp
// CIR: cir.call_llvm_intrinsic "amdgcn.fcmp" {{.*}} : (!cir.double, !cir.double, !s32i) -> !u64i
// LLVM: call i64 @llvm.amdgcn.fcmp.i64.f64(double %{{.*}}, double %{{.*}}, i32 1)
// OGCG: call i64 @llvm.amdgcn.fcmp.i64.f64(double %{{.*}}, double %{{.*}}, i32 1)
void test_fcmp(global ulong *out, double a, double b) {
  *out = __builtin_amdgcn_fcmp(a, b, 1);
}

// CIR-LABEL: @test_sin_f32
// CIR: cir.call_llvm_intrinsic "amdgcn.sin" {{.*}} : (!cir.float) -> !cir.float
// LLVM: call float @llvm.amdgcn.sin.f32(float %{{.*}})
// OGCG: call float @llvm.amdgcn.sin.f32(float %{{.*}})
void test_sin_f32(global float *out, float a) {
  *out = __builtin_amdgcn_sinf(a);
}

// CIR-LABEL: @test_log_f32
// CIR: cir.call_llvm_intrinsic "amdgcn.log" {{.*}} : (!cir.float) -> !cir.float
// LLVM: call float @llvm.amdgcn.log.f32(float %{{.*}})
// OGCG: call float @llvm.amdgcn.log.f32(float %{{.*}})
void test_log_f32(global float *out, float a) {
  *out = __builtin_amdgcn_logf(a);
}

// CIR-LABEL: @test_exp2_f32
// CIR: cir.call_llvm_intrinsic "amdgcn.exp2" {{.*}} : (!cir.float) -> !cir.float
// LLVM: call float @llvm.amdgcn.exp2.f32(float %{{.*}})
// OGCG: call float @llvm.amdgcn.exp2.f32(float %{{.*}})
void test_exp2_f32(global float *out, float a) {
  *out = __builtin_amdgcn_exp2f(a);
}

// CIR-LABEL: @test_rsq_clamp_f32
// CIR: cir.call_llvm_intrinsic "amdgcn.rsq.clamp" {{.*}} : (!cir.float) -> !cir.float
// LLVM: call float @llvm.amdgcn.rsq.clamp.f32(float %{{.*}})
// OGCG: call float @llvm.amdgcn.rsq.clamp.f32(float %{{.*}})
void test_rsq_clamp_f32(global float *out, float a) {
  *out = __builtin_amdgcn_rsq_clampf(a);
}

// CIR-LABEL: @test_dispatch_ptr
// CIR: cir.call_llvm_intrinsic "amdgcn.dispatch.ptr" {{.*}} -> !cir.ptr<!s8i, target_address_space(4)> {res_attrs = [{llvm.align = 4 : i64, llvm.dereferenceable = 64 : i64}]}
// LLVM: call align 4 dereferenceable(64) ptr addrspace(4) @llvm.amdgcn.dispatch.ptr()
// OGCG: call align 4 dereferenceable(64) ptr addrspace(4) @llvm.amdgcn.dispatch.ptr()
void test_dispatch_ptr(global uint *out) {
  __constant void *p = __builtin_amdgcn_dispatch_ptr();
  *out = ((__constant uint *)p)[0];
}
