// RUN: %clang_cc1 -triple amdgcn-unknown-unknown -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple amdgcn-unknown-unknown -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Test AMDGPU aggregate argument ABI classification for non-kernel (stub)
// functions. Multi-element aggregates should be coerced to integer types:
//   <= 16 bits -> i16
//   <= 32 bits -> i32
//   <= 64 bits -> [2 x i32]
// Single-element structs should be unwrapped to the element type.

// --- Single-element struct: unwrapped to float ---
struct Single { float x; };

// Non-kernel function: single-element struct passed as float directly.
float use_single(struct Single s) { return s.x; }

// CIR-DAG: define dso_local {{.*}}float @use_single(float {{.*}}%
// OGCG-DAG: define dso_local float @use_single(float {{.*}}%

// Kernel with single-element struct: unwrapped to float (not byref).
kernel void test_single(global float *out, struct Single s) {
  out[0] = s.x;
}

// CIR-DAG: define dso_local void @__clang_ocl_kern_imp_test_single(ptr addrspace(1){{.*}}, float %
// OGCG-DAG: define dso_local void @__clang_ocl_kern_imp_test_single(ptr addrspace(1){{.*}}, float %

// CIR-DAG: define dso_local amdgpu_kernel void @test_single(ptr addrspace(1){{.*}}, float %
// OGCG-DAG: define dso_local amdgpu_kernel void @test_single(ptr addrspace(1){{.*}}, float %

// Return single-element struct: unwrapped to float.
struct Single ret_single() {
  struct Single r = { 1.0f };
  return r;
}

// CIR-DAG: define dso_local {{.*}}float @ret_single()
// OGCG-DAG: define dso_local {{.*}}float @ret_single()

// --- 64-bit struct (2 floats): coerced to [2 x i32] in stub ---
struct S64 { float x, y; };

kernel void test_s64(global float *out, struct S64 s) {
  out[0] = s.x + s.y;
}

// Stub takes [2 x i32] coercion type.
// CIR-DAG: define dso_local void @__clang_ocl_kern_imp_test_s64(ptr addrspace(1){{.*}}, [2 x i32] %
// OGCG-DAG: define dso_local void @__clang_ocl_kern_imp_test_s64(ptr addrspace(1){{.*}}, [2 x i32] %

// Kernel entry takes byref in constant address space.
// CIR-DAG: define dso_local amdgpu_kernel void @test_s64(ptr addrspace(1){{.*}}, ptr addrspace(4){{.*}} byref(%struct.S64) align 4
// OGCG-DAG: define dso_local amdgpu_kernel void @test_s64(ptr addrspace(1){{.*}}, ptr addrspace(4){{.*}} byref(%struct.S64) align 4

// --- 32-bit struct (2 shorts): coerced to i32 in stub ---
struct S32 { short a, b; };

kernel void test_s32(global short *out, struct S32 s) {
  out[0] = s.a + s.b;
}

// CIR-DAG: define dso_local void @__clang_ocl_kern_imp_test_s32(ptr addrspace(1){{.*}}, i32 %
// OGCG-DAG: define dso_local void @__clang_ocl_kern_imp_test_s32(ptr addrspace(1){{.*}}, i32 %

// --- 16-bit struct (2 chars): coerced to i16 in stub ---
struct S16 { char a, b; };

kernel void test_s16(global char *out, struct S16 s) {
  out[0] = s.a + s.b;
}

// CIR-DAG: define dso_local void @__clang_ocl_kern_imp_test_s16(ptr addrspace(1){{.*}}, i16 %
// OGCG-DAG: define dso_local void @__clang_ocl_kern_imp_test_s16(ptr addrspace(1){{.*}}, i16 %

// --- Return 64-bit struct: coerced to [2 x i32] ---
struct S64 return_s64() {
  struct S64 r = { 1.0f, 2.0f };
  return r;
}

// CIR-DAG: define dso_local [2 x i32] @return_s64()
// OGCG-DAG: define dso_local [2 x i32] @return_s64()

// --- Non-kernel device function with struct arg ---
float device_with_struct(struct S64 s) {
  return s.x + s.y;
}

// CIR-DAG: define dso_local {{.*}}float @device_with_struct([2 x i32] %
// OGCG-DAG: define dso_local {{.*}}float @device_with_struct([2 x i32] %

// --- Return 32-bit struct: coerced to i32 ---
struct S32 return_s32() {
  struct S32 r = { 1, 2 };
  return r;
}

// CIR-DAG: define dso_local {{.*}}i32 @return_s32()
// OGCG-DAG: define dso_local {{.*}}i32 @return_s32()

// --- Single-element struct with array[1] member: unwrapped to float ---
struct ArrSingle { float arr[1]; };
float use_arr_single(struct ArrSingle s) { return s.arr[0]; }
struct ArrSingle ret_arr_single() {
  struct ArrSingle r = {{1.0f}};
  return r;
}

// CIR-DAG: define dso_local {{.*}}float @use_arr_single(float {{.*}}%
// OGCG-DAG: define dso_local float @use_arr_single(float {{.*}}%

// CIR-DAG: define dso_local {{.*}}float @ret_arr_single()
// OGCG-DAG: define dso_local {{.*}}float @ret_arr_single()
