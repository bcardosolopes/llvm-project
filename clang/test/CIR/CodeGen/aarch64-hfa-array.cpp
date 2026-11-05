// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Verify array-member HFA detection on AArch64.

struct Vec4 { float data[4]; };

// CHECK: void @_Z8use_vec4{{.*}}([4 x float] alignstack(8) %{{.*}})
void use_vec4(Vec4 v) {}

// Nested array: float[2][2] = 4 floats.
struct Mat2 { float m[2][2]; };

// CHECK: void @_Z8use_mat2{{.*}}([4 x float] alignstack(8) %{{.*}})
void use_mat2(Mat2 m) {}
