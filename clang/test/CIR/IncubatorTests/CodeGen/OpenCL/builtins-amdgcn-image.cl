// REQUIRES: amdgpu-registered-target
// RUN: %clang_cc1 -triple amdgcn-unknown-unknown -cl-std=CL2.0 -fclangir\
// RUN: -target-cpu gfx1100 -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s

// RUN: %clang_cc1 -triple amdgcn-unknown-unknown -cl-std=CL2.0 -fclangir\
// RUN: -target-cpu gfx1100 -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

// RUN: %clang_cc1 -triple amdgcn-unknown-unknown -cl-std=CL2.0\
// RUN: -target-cpu gfx1100 -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ll %s

//===----------------------------------------------------------------------===//
// Test AMDGPU image load/store builtins
//===----------------------------------------------------------------------===//

typedef float float4 __attribute__((ext_vector_type(4)));
typedef half half4 __attribute__((ext_vector_type(4)));

// CIR-LABEL: @test_image_load_2d_f32
// CIR-SAME: %arg3: !cir.ptr<!void>
// CIR: %{{.*}} = cir.load align(32) %{{.*}} : !cir.ptr<!cir.ptr<!void>, lang_address_space(offload_private)>, !cir.ptr<!void>
// CIR-NEXT: %{{.*}} = cir.cast bitcast %{{.*}} : !cir.ptr<!void> -> !cir.ptr<!cir.vector<8 x !s32i>>
// CIR-NEXT: %{{.*}} = cir.load align(32) %{{.*}} : !cir.ptr<!cir.vector<8 x !s32i>>, !cir.vector<8 x !s32i>
// CIR: cir.call_llvm_intrinsic "amdgcn.image.load.2d" {{.*}} : (!s32i, !s32i, !s32i, !cir.vector<8 x !s32i>, !s32i, !s32i) -> !cir.float
// LLVM: define{{.*}} void @test_image_load_2d_f32(
// LLVM: call {{.*}}float @llvm.amdgcn.image.load.2d.f32.i32.v8i32(i32 {{.*}}, i32 {{.*}}, i32 {{.*}}, <8 x i32> {{.*}}, i32 {{.*}}, i32 {{.*}})
// OGCG: define{{.*}} void @test_image_load_2d_f32(
// OGCG: call {{.*}}float @llvm.amdgcn.image.load.2d.f32.i32.v8i32(i32 {{.*}}, i32 {{.*}}, i32 {{.*}}, <8 x i32> {{.*}}, i32 {{.*}}, i32 {{.*}})
void test_image_load_2d_f32(global float* out, int x, int y, __amdgpu_texture_t rsrc) {
  *out = __builtin_amdgcn_image_load_2d_f32_i32(15, x, y, rsrc, 0, 0);
}

// CIR-LABEL: @test_image_load_2d_v4f32
// CIR: cir.call_llvm_intrinsic "amdgcn.image.load.2d" {{.*}} : (!s32i, !s32i, !s32i, !cir.vector<8 x !s32i>, !s32i, !s32i) -> !cir.vector<4 x !cir.float>
// LLVM: define{{.*}} void @test_image_load_2d_v4f32(
// LLVM: call {{.*}}<4 x float> @llvm.amdgcn.image.load.2d.v4f32.i32.v8i32(i32 {{.*}}, i32 {{.*}}, i32 {{.*}}, <8 x i32> {{.*}}, i32 {{.*}}, i32 {{.*}})
// OGCG: define{{.*}} void @test_image_load_2d_v4f32(
// OGCG: call {{.*}}<4 x float> @llvm.amdgcn.image.load.2d.v4f32.i32.v8i32(i32 {{.*}}, i32 {{.*}}, i32 {{.*}}, <8 x i32> {{.*}}, i32 {{.*}}, i32 {{.*}})
void test_image_load_2d_v4f32(global float4* out, int x, int y, __amdgpu_texture_t rsrc) {
  *out = __builtin_amdgcn_image_load_2d_v4f32_i32(15, x, y, rsrc, 0, 0);
}

// CIR-LABEL: @test_image_load_2d_v4f16
// CIR: cir.call_llvm_intrinsic "amdgcn.image.load.2d" {{.*}} : (!s32i, !s32i, !s32i, !cir.vector<8 x !s32i>, !s32i, !s32i) -> !cir.vector<4 x !cir.f16>
// LLVM: define{{.*}} void @test_image_load_2d_v4f16(
// LLVM: call {{.*}}<4 x half> @llvm.amdgcn.image.load.2d.v4f16.i32.v8i32(i32 {{.*}}, i32 {{.*}}, i32 {{.*}}, <8 x i32> {{.*}}, i32 {{.*}}, i32 {{.*}})
// OGCG: define{{.*}} void @test_image_load_2d_v4f16(
// OGCG: call {{.*}}<4 x half> @llvm.amdgcn.image.load.2d.v4f16.i32.v8i32(i32 {{.*}}, i32 {{.*}}, i32 {{.*}}, <8 x i32> {{.*}}, i32 {{.*}}, i32 {{.*}})
void test_image_load_2d_v4f16(global half4* out, int x, int y, __amdgpu_texture_t rsrc) {
  *out = __builtin_amdgcn_image_load_2d_v4f16_i32(15, x, y, rsrc, 0, 0);
}

// CIR-LABEL: @test_image_store_2d_f32
// CIR: cir.call_llvm_intrinsic "amdgcn.image.store.2d" {{.*}} : (!cir.float, !s32i, !s32i, !s32i, !cir.vector<8 x !s32i>, !s32i, !s32i) -> !void
// LLVM: define{{.*}} void @test_image_store_2d_f32(
// LLVM: call void @llvm.amdgcn.image.store.2d.f32.i32.v8i32(float {{.*}}, i32 {{.*}}, i32 {{.*}}, i32 {{.*}}, <8 x i32> {{.*}}, i32 {{.*}}, i32 {{.*}})
// OGCG: define{{.*}} void @test_image_store_2d_f32(
// OGCG: call void @llvm.amdgcn.image.store.2d.f32.i32.v8i32(float {{.*}}, i32 {{.*}}, i32 {{.*}}, i32 {{.*}}, <8 x i32> {{.*}}, i32 {{.*}}, i32 {{.*}})
void test_image_store_2d_f32(float val, int x, int y, __amdgpu_texture_t rsrc) {
  __builtin_amdgcn_image_store_2d_f32_i32(val, 15, x, y, rsrc, 0, 0);
}

// CIR-LABEL: @test_image_store_2d_v4f32
// CIR: cir.call_llvm_intrinsic "amdgcn.image.store.2d" {{.*}} : (!cir.vector<4 x !cir.float>, !s32i, !s32i, !s32i, !cir.vector<8 x !s32i>, !s32i, !s32i) -> !void
// LLVM: define{{.*}} void @test_image_store_2d_v4f32(
// LLVM: call void @llvm.amdgcn.image.store.2d.v4f32.i32.v8i32(<4 x float> {{.*}}, i32 {{.*}}, i32 {{.*}}, i32 {{.*}}, <8 x i32> {{.*}}, i32 {{.*}}, i32 {{.*}})
// OGCG: define{{.*}} void @test_image_store_2d_v4f32(
// OGCG: call void @llvm.amdgcn.image.store.2d.v4f32.i32.v8i32(<4 x float> {{.*}}, i32 {{.*}}, i32 {{.*}}, i32 {{.*}}, <8 x i32> {{.*}}, i32 {{.*}}, i32 {{.*}})
void test_image_store_2d_v4f32(float4 val, int x, int y, __amdgpu_texture_t rsrc) {
  __builtin_amdgcn_image_store_2d_v4f32_i32(val, 15, x, y, rsrc, 0, 0);
}

// CIR-LABEL: @test_image_store_2d_v4f16
// CIR: cir.call_llvm_intrinsic "amdgcn.image.store.2d" {{.*}} : (!cir.vector<4 x !cir.f16>, !s32i, !s32i, !s32i, !cir.vector<8 x !s32i>, !s32i, !s32i) -> !void
// LLVM: define{{.*}} void @test_image_store_2d_v4f16(
// LLVM: call void @llvm.amdgcn.image.store.2d.v4f16.i32.v8i32(<4 x half> {{.*}}, i32 {{.*}}, i32 {{.*}}, i32 {{.*}}, <8 x i32> {{.*}}, i32 {{.*}}, i32 {{.*}})
// OGCG: define{{.*}} void @test_image_store_2d_v4f16(
// OGCG: call void @llvm.amdgcn.image.store.2d.v4f16.i32.v8i32(<4 x half> {{.*}}, i32 {{.*}}, i32 {{.*}}, i32 {{.*}}, <8 x i32> {{.*}}, i32 {{.*}}, i32 {{.*}})
void test_image_store_2d_v4f16(half4 val, int x, int y, __amdgpu_texture_t rsrc) {
  __builtin_amdgcn_image_store_2d_v4f16_i32(val, 15, x, y, rsrc, 0, 0);
}

// CIR-LABEL: @test_image_load_2darray_f32
// CIR: cir.call_llvm_intrinsic "amdgcn.image.load.2darray" {{.*}} : (!s32i, !s32i, !s32i, !s32i, !cir.vector<8 x !s32i>, !s32i, !s32i) -> !cir.float
// LLVM: define{{.*}} void @test_image_load_2darray_f32(
// LLVM: call {{.*}}float @llvm.amdgcn.image.load.2darray.f32.i32.v8i32(i32 {{.*}}, i32 {{.*}}, i32 {{.*}}, i32 {{.*}}, <8 x i32> {{.*}}, i32 {{.*}}, i32 {{.*}})
// OGCG: define{{.*}} void @test_image_load_2darray_f32(
// OGCG: call {{.*}}float @llvm.amdgcn.image.load.2darray.f32.i32.v8i32(i32 {{.*}}, i32 {{.*}}, i32 {{.*}}, i32 {{.*}}, <8 x i32> {{.*}}, i32 {{.*}}, i32 {{.*}})
void test_image_load_2darray_f32(global float* out, int x, int y, int slice, __amdgpu_texture_t rsrc) {
  *out = __builtin_amdgcn_image_load_2darray_f32_i32(15, x, y, slice, rsrc, 0, 0);
}

// CIR-LABEL: @test_image_load_2darray_v4f32
// CIR: cir.call_llvm_intrinsic "amdgcn.image.load.2darray" {{.*}} : (!s32i, !s32i, !s32i, !s32i, !cir.vector<8 x !s32i>, !s32i, !s32i) -> !cir.vector<4 x !cir.float>
// LLVM: define{{.*}} void @test_image_load_2darray_v4f32(
// LLVM: call {{.*}}<4 x float> @llvm.amdgcn.image.load.2darray.v4f32.i32.v8i32(i32 {{.*}}, i32 {{.*}}, i32 {{.*}}, i32 {{.*}}, <8 x i32> {{.*}}, i32 {{.*}}, i32 {{.*}})
// OGCG: define{{.*}} void @test_image_load_2darray_v4f32(
// OGCG: call {{.*}}<4 x float> @llvm.amdgcn.image.load.2darray.v4f32.i32.v8i32(i32 {{.*}}, i32 {{.*}}, i32 {{.*}}, i32 {{.*}}, <8 x i32> {{.*}}, i32 {{.*}}, i32 {{.*}})
void test_image_load_2darray_v4f32(global float4* out, int x, int y, int slice, __amdgpu_texture_t rsrc) {
  *out = __builtin_amdgcn_image_load_2darray_v4f32_i32(15, x, y, slice, rsrc, 0, 0);
}

// CIR-LABEL: @test_image_store_2darray_f32
// CIR: cir.call_llvm_intrinsic "amdgcn.image.store.2darray" {{.*}} : (!cir.float, !s32i, !s32i, !s32i, !s32i, !cir.vector<8 x !s32i>, !s32i, !s32i) -> !void
// LLVM: define{{.*}} void @test_image_store_2darray_f32(
// LLVM: call void @llvm.amdgcn.image.store.2darray.f32.i32.v8i32(float {{.*}}, i32 {{.*}}, i32 {{.*}}, i32 {{.*}}, i32 {{.*}}, <8 x i32> {{.*}}, i32 {{.*}}, i32 {{.*}})
// OGCG: define{{.*}} void @test_image_store_2darray_f32(
// OGCG: call void @llvm.amdgcn.image.store.2darray.f32.i32.v8i32(float {{.*}}, i32 {{.*}}, i32 {{.*}}, i32 {{.*}}, i32 {{.*}}, <8 x i32> {{.*}}, i32 {{.*}}, i32 {{.*}})
void test_image_store_2darray_f32(float val, int x, int y, int slice, __amdgpu_texture_t rsrc) {
  __builtin_amdgcn_image_store_2darray_f32_i32(val, 15, x, y, slice, rsrc, 0, 0);
}

// CIR-LABEL: @test_image_store_2darray_v4f32
// CIR: cir.call_llvm_intrinsic "amdgcn.image.store.2darray" {{.*}} : (!cir.vector<4 x !cir.float>, !s32i, !s32i, !s32i, !s32i, !cir.vector<8 x !s32i>, !s32i, !s32i) -> !void
// LLVM: define{{.*}} void @test_image_store_2darray_v4f32(
// LLVM: call void @llvm.amdgcn.image.store.2darray.v4f32.i32.v8i32(<4 x float> {{.*}}, i32 {{.*}}, i32 {{.*}}, i32 {{.*}}, i32 {{.*}}, <8 x i32> {{.*}}, i32 {{.*}}, i32 {{.*}})
// OGCG: define{{.*}} void @test_image_store_2darray_v4f32(
// OGCG: call void @llvm.amdgcn.image.store.2darray.v4f32.i32.v8i32(<4 x float> {{.*}}, i32 {{.*}}, i32 {{.*}}, i32 {{.*}}, i32 {{.*}}, <8 x i32> {{.*}}, i32 {{.*}}, i32 {{.*}})
void test_image_store_2darray_v4f32(float4 val, int x, int y, int slice, __amdgpu_texture_t rsrc) {
  __builtin_amdgcn_image_store_2darray_v4f32_i32(val, 15, x, y, slice, rsrc, 0, 0);
}
