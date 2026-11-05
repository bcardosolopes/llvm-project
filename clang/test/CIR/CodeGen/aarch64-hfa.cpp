// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Verify AArch64 HFA (Homogeneous Floating-point Aggregate) classification.
// Structs of 1-4 same-type floats/doubles should be passed in SIMD registers.

struct Vec4f { float x, y, z, w; };
struct Vec2d { double x, y; };

// CHECK: void @_Z8use_vec45Vec4f([4 x float] alignstack(8) %{{.*}})
void use_vec4(Vec4f v) {}

// CHECK: void @_Z9use_vec2d5Vec2d([2 x double] alignstack(8) %{{.*}})
void use_vec2d(Vec2d v) {}
