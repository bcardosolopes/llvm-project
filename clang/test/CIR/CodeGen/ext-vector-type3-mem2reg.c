// RUN: %clang_cc1 -triple i386-unknown-linux-gnu -fclangir -fclangir-mem2reg \
// RUN:   -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Regression test: ext_vector_type(3) compound literal with
// -fclangir-mem2reg would crash in LLVM's constant folder (FoldBitCast)
// when IRBuilder::CreateShuffleVector tried to constant-fold a shuffle
// between <3 x float> and <4 x float> (different-sized vectors).
// The fix manually folds shufflevectors with constant/poison operands
// when input and output vector sizes differ.

typedef float __attribute__((ext_vector_type(3))) float3;

void assign_vec3(float3 *p) {
  *p = (float3) { 1.0f, 2.0f, 3.0f };
}
// CHECK-LABEL: define {{.*}}void @assign_vec3(
// CHECK: store <4 x float>
// CHECK: ret void

// Also test with reduced alignment (the alignment.c test3 pattern).
typedef float __attribute__((ext_vector_type(3), aligned(4))) packedfloat3;
void assign_packed_vec3(packedfloat3 *p) {
  *p = (packedfloat3) { 3.2f, 2.3f, 0.1f };
}
// CHECK-LABEL: define {{.*}}void @assign_packed_vec3(
// CHECK: store <4 x float> {{.*}}, align 4
// CHECK: ret void
