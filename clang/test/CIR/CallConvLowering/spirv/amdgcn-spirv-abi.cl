// RUN: %clang_cc1 -triple spirv64-amd-amdhsa -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple spirv64-amd-amdhsa -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Comprehensive AMDGCN SPIR-V ABI classification tests.
// Tests empty structs, single-element structs, nested structs,
// multi-element struct coercion, kernel vs non-kernel, and return types.

// --- Empty struct: ignored in non-kernel functions ---
struct Empty {};

void use_empty(struct Empty e) {}

// CIR-DAG: define{{.*}} spir_func void @use_empty()
// OGCG-DAG: define{{.*}} spir_func void @use_empty()

struct Empty ret_empty() { struct Empty e; return e; }

// CIR-DAG: define{{.*}} spir_func void @ret_empty()
// OGCG-DAG: define{{.*}} spir_func void @ret_empty()

// --- Single-element struct: unwrapped to scalar ---
struct Single { float x; };

float use_single(struct Single s) { return s.x; }

// CIR-DAG: define{{.*}} spir_func {{.*}}float @use_single(float {{.*}}%
// OGCG-DAG: define{{.*}} spir_func float @use_single(float {{.*}}%

struct Single ret_single() { struct Single s = {1.0f}; return s; }

// CIR-DAG: define{{.*}} spir_func {{.*}}float @ret_single()
// OGCG-DAG: define{{.*}} spir_func {{.*}}float @ret_single()

// --- Nested single-element struct: unwrapped to leaf scalar ---
struct Outer { struct Single inner; };

float use_nested(struct Outer o) { return o.inner.x; }

// CIR-DAG: define{{.*}} spir_func {{.*}}float @use_nested(float {{.*}}%
// OGCG-DAG: define{{.*}} spir_func float @use_nested(float {{.*}}%

// --- 64-bit struct: coerced to [2 x i32] ---
struct S64 { float x, y; };

float use_s64(struct S64 s) { return s.x + s.y; }

// CIR-DAG: define{{.*}} spir_func {{.*}}float @use_s64([2 x i32] %
// OGCG-DAG: define{{.*}} spir_func float @use_s64([2 x i32] %

struct S64 ret_s64() { struct S64 s = {1.0f, 2.0f}; return s; }

// CIR-DAG: define{{.*}} spir_func [2 x i32] @ret_s64()
// OGCG-DAG: define{{.*}} spir_func [2 x i32] @ret_s64()

// --- 96-bit struct: passed directly (fits in regs) ---
struct S96 { float x, y, z; };

float use_s96(struct S96 s) { return s.x + s.y + s.z; }

// CIR-DAG: define{{.*}} spir_func {{.*}}float @use_s96(float %{{.*}}, float %{{.*}}, float %
// OGCG-DAG: define{{.*}} spir_func float @use_s96(float %{{.*}}, float %{{.*}}, float %

// --- Kernel with struct: byref in constant address space ---
kernel void kern_s64(global float *out, struct S64 s) {
  out[0] = s.x + s.y;
}

// Kernel entry uses byref with addrspace(2) (opencl_constant).
// CIR-DAG: define{{.*}} spir_kernel void @kern_s64(ptr addrspace(1){{.*}}, ptr addrspace(2){{.*}} byref(%struct.S64) align 4
// OGCG-DAG: define{{.*}} spir_kernel void @kern_s64(ptr addrspace(1){{.*}}, ptr addrspace(2){{.*}} byref(%struct.S64) align 4

// Stub uses [2 x i32] coercion.
// CIR-DAG: define{{.*}} spir_func void @__clang_ocl_kern_imp_kern_s64(ptr addrspace(1){{.*}}, [2 x i32] %
// OGCG-DAG: define{{.*}} spir_func void @__clang_ocl_kern_imp_kern_s64(ptr addrspace(1){{.*}}, [2 x i32] %

// --- Kernel with single-element struct: unwrapped to scalar ---
kernel void kern_single(global float *out, struct Single s) {
  out[0] = s.x;
}

// CIR-DAG: define{{.*}} spir_kernel void @kern_single(ptr addrspace(1){{.*}}, float %
// OGCG-DAG: define{{.*}} spir_kernel void @kern_single(ptr addrspace(1){{.*}}, float %

// --- Register exhaustion: large struct falls back to byref when regs full ---
struct Big { float a, b, c, d, e, f, g, h; };

// 8 scalar args (8 regs) + 8-field struct (8 regs) = 16: exactly fits.
float use_big_fit(float a, float b, float c, float d,
                  float e, float f, float g, float h,
                  struct Big s) { return s.a; }

// CIR-DAG: define{{.*}} spir_func {{.*}}float @use_big_fit(float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}returned {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float %
// OGCG-DAG: define{{.*}} spir_func float @use_big_fit(float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}returned {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float %

// 9 scalar args (9 regs) + 8-field struct = 17: exceeds 16 → byref.
float use_big_nofit(float a, float b, float c, float d,
                    float e, float f, float g, float h,
                    float i,
                    struct Big s) { return s.a; }

// CIR-DAG: define{{.*}} spir_func {{.*}}float @use_big_nofit(float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, ptr {{.*}}byref(%struct.Big) align 4
// OGCG-DAG: define{{.*}} spir_func float @use_big_nofit(float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, ptr {{.*}}byref(%struct.Big) align 4
