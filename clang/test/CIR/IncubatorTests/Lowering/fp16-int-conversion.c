// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Verify that integer-to-__fp16 conversion goes through float intermediate,
// matching OGCG behavior (sitofp/uitofp to float, then fptrunc to half).

void int_to_fp16(int x, __fp16 *out) { *out = (__fp16)x; }
// CHECK-LABEL: define {{.*}}@int_to_fp16(
// CHECK: sitofp i32 %{{.*}} to float
// CHECK: fptrunc float %{{.*}} to half

void uint_to_fp16(unsigned x, __fp16 *out) { *out = (__fp16)x; }
// CHECK-LABEL: define {{.*}}@uint_to_fp16(
// CHECK: uitofp i32 %{{.*}} to float
// CHECK: fptrunc float %{{.*}} to half

// Float-to-half should be a single fptrunc (no intermediate).
void float_to_fp16(float x, __fp16 *out) { *out = (__fp16)x; }
// CHECK-LABEL: define {{.*}}@float_to_fp16(
// CHECK: fptrunc float %{{.*}} to half
// CHECK-NOT: fptrunc
