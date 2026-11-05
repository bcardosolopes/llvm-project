// RUN: %clang_cc1 -triple amdgcn-unknown-unknown -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple amdgcn-unknown-unknown -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Comprehensive AMDGPU ABI classify function tests.
// Tests empty structs, nested structs, large structs, and various
// scalar/aggregate combinations for both kernel and non-kernel functions.

// --- Empty struct: ignored in non-kernel functions ---
struct Empty {};

void use_empty(struct Empty e) {}

// The empty struct argument should be ignored (not present in signature).
// CIR-DAG: define dso_local void @use_empty()
// OGCG-DAG: define dso_local void @use_empty()

struct Empty ret_empty() { struct Empty e; return e; }

// CIR-DAG: define dso_local void @ret_empty()
// OGCG-DAG: define dso_local void @ret_empty()

// --- Nested single-element struct: unwrapped to leaf scalar ---
struct Inner { float x; };
struct Outer { struct Inner inner; };

float use_nested(struct Outer o) { return o.inner.x; }

// CIR-DAG: define dso_local {{.*}}float @use_nested(float {{.*}}%
// OGCG-DAG: define dso_local float @use_nested(float {{.*}}%

struct Outer ret_nested() { struct Outer o = {{1.0f}}; return o; }

// CIR-DAG: define dso_local {{.*}}float @ret_nested()
// OGCG-DAG: define dso_local {{.*}}float @ret_nested()

// --- Large struct (>64 bits, <=16 regs): passed directly ---
struct S96 { float x, y, z; };

float use_s96(struct S96 s) { return s.x + s.y + s.z; }

// CIR-DAG: define dso_local {{.*}}float @use_s96(float %{{.*}}, float %{{.*}}, float %
// OGCG-DAG: define dso_local float @use_s96(float %{{.*}}, float %{{.*}}, float %

// --- Kernel with struct: byref in constant address space ---
struct S64 { float x, y; };

kernel void kern_s64(global float *out, struct S64 s) {
  out[0] = s.x + s.y;
}

// Kernel entry uses byref.
// CIR-DAG: define dso_local amdgpu_kernel void @kern_s64(ptr addrspace(1){{.*}}, ptr addrspace(4){{.*}} byref(%struct.S64) align 4
// OGCG-DAG: define dso_local amdgpu_kernel void @kern_s64(ptr addrspace(1){{.*}}, ptr addrspace(4){{.*}} byref(%struct.S64) align 4

// Stub uses [2 x i32] coercion.
// CIR-DAG: define dso_local void @__clang_ocl_kern_imp_kern_s64(ptr addrspace(1){{.*}}, [2 x i32] %
// OGCG-DAG: define dso_local void @__clang_ocl_kern_imp_kern_s64(ptr addrspace(1){{.*}}, [2 x i32] %

// --- Kernel with single-element struct: unwrapped to scalar ---
kernel void kern_single(global float *out, struct Inner s) {
  out[0] = s.x;
}

// CIR-DAG: define dso_local amdgpu_kernel void @kern_single(ptr addrspace(1){{.*}}, float %
// OGCG-DAG: define dso_local amdgpu_kernel void @kern_single(ptr addrspace(1){{.*}}, float %

// --- Register exhaustion: large struct falls back to indirect ---
struct Big { float a, b, c, d, e, f, g, h; };

// 8 scalar args + 8-field struct = 16 regs: exactly fits → direct.
float use_big_fit(float a, float b, float c, float d,
                  float e, float f, float g, float h,
                  struct Big s) { return s.a; }

// CIR-DAG: define dso_local {{.*}}float @use_big_fit(float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}returned {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float %
// OGCG-DAG: define dso_local float @use_big_fit(float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}returned {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float %

// 9 scalar args + 8-field struct = 17 regs: exceeds 16 → indirect byref.
float use_big_nofit(float a, float b, float c, float d,
                    float e, float f, float g, float h,
                    float i,
                    struct Big s) { return s.a; }

// CIR-DAG: define dso_local {{.*}}float @use_big_nofit(float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, ptr addrspace(5) {{.*}}byref(%struct.Big) align 4
// OGCG-DAG: define dso_local float @use_big_nofit(float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, ptr addrspace(5) {{.*}}byref(%struct.Big) align 4
